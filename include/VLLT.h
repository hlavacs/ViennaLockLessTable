#pragma once

#include <assert.h>
#include <algorithm>
#include <memory_resource>
#include <shared_mutex>
#include <optional>
#include <array>
#include <stack>
#include <concepts>
#include <algorithm>
#include <type_traits>
#include <vector>
#include <queue>
#include <optional>
#include <thread>
#include <latch>
#include <numeric>
#include <string>
#include <cstdlib>
#include <random>
#include <functional>

#include "VTLL.h"
#include "VSTY.h"

//todo: partition table indices into state/counter, turn spinlock into lockless with state/counter, 
//align atomics, block allocation on demand not all alloc when constructing, also pay constr/destr costs,
//read-write locking blocks: push - dont have to do anything (even if other are read/writing last block), 
//pop - write lock the last block

namespace vllt {

	class VlltSpinlock {
	public:
		void lock() { 
			int flag = m_flag.load(std::memory_order_relaxed);
			for( int i=0; flag!=0 || m_flag.compare_exchange_weak(flag, -1); ++i ) { 
				if (i == 8) { std::this_thread::sleep_for(std::chrono::nanoseconds(1)); i = 0; } 
			}
		} 

		void unlock() { m_flag.store(0, std::memory_order_release); } 

		bool try_lock() { 
			int flag = 0;
			return m_flag.compare_exchange_strong(flag, -1);
		}

		void shared_lock() { 
			int flag = m_flag.load(std::memory_order_relaxed);
			for( int i=0; flag<0 || m_flag.compare_exchange_weak(flag, flag+1); ++i ) { 
				if (i == 8) { std::this_thread::sleep_for(std::chrono::nanoseconds(1)); i = 0; } 
			}
		} 

		void shared_unlock() {
			--m_flag;
		}

		bool try_shared_lock() { 
			int flag = m_flag.load(std::memory_order_relaxed);
			for(int i=0; flag >= 0 && !m_flag.compare_exchange_strong(flag, flag+1); ++i) {
				if (i == 8) { std::this_thread::sleep_for(std::chrono::nanoseconds(1)); i = 0; }
			}
			return flag >= 0;
		}

	private:
		std::atomic<int> m_flag{0};
	};


	//---------------------------------------------------------------------------------------------------

	using table_index_t = vsty::strong_type_t<uint64_t, vsty::counter<>, std::integral_constant<uint64_t, std::numeric_limits<uint64_t>::max()>>;///< Strong integer type for indexing rows, 0 to number rows - 1
	using push_callback_t = std::optional<std::function<void(const table_index_t)>>; ///< Callback function that is called when a new row is pushed

	//---------------------------------------------------------------------------------------------------

	enum sync_t {
		VLLT_SYNC_EXTERNAL = 0,
		VLLT_SYNC_INTERNAL = 1,
		VLLT_SYNC_DEBUG = 2
	};

	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR>
	concept VlltStaticTableConcept = vtll::unique<DATA>::value;

	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR>
		requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	class VlltStaticTable;

	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
	concept VlltStaticTableViewConcept = (
		VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR> 
		&& (vtll::size< vtll::intersection< vtll::tl<READ, WRITE>> >::value == 0) 
		&& (vtll::has_all_types<DATA, READ>::value) 
		&& (vtll::has_all_types<DATA, WRITE>::value)
	);

	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
		//requires VlltStaticTableViewConcept<DATA, N0, ROW, MINSLOTS, FAIR, READ, WRITE>
	class VlltStaticTableView;



	/// <summary>
	/// VlltTable is the base class for some classes, enabling management of tables that can be
	// appended in parallel.
	/// </summary>
	/// <typeparam name="DATA">List of component types. All types must be default constructible.</typeparam>
	/// <typeparam name="N0">Number of slots per block. Is rounded to the next power of 2.</typeparam>
	/// <typeparam name="ROW">If true, then data is stored rowwise, else columnwise.</typeparam>
	/// <typeparam name="MINSLOTS">Miniumum number of available slots in the block map.</typeparam>
	/// <typeparam name="FAIR">Balance adding and popping to prevent starving.</typeparam>
	template<typename DATA, sync_t SYNC = VLLT_SYNC_EXTERNAL, size_t N0 = 1 << 5, bool ROW = true, size_t MINSLOTS = 16, bool FAIR = false>
		requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	class VlltStaticTable {

		public:
		template<typename U1, sync_t U2, size_t U3, bool U4, size_t U5, bool U6, typename U7, typename U8>
			//requires VlltStaticTableViewConcept<U1, U2, U3, U4, U5, U6, U7, U8>
		friend class VlltStaticTableView;


	protected:
		static_assert(std::is_default_constructible_v<DATA>, "Your components are not default constructible!");

		const size_t NUMBITS1 = 44; ///< Number of bits for the index of the first item in the stack
		using table_diff_t  = vsty::strong_type_t<uint64_t, vsty::counter<>, std::integral_constant<uint64_t, std::numeric_limits<uint64_t>::max()>>;
		using block_idx_t = vsty::strong_type_t<size_t, vsty::counter<>>; ///< Strong integer type for indexing blocks, 0 to size map - 1

		static const size_t N = vtll::smallest_pow2_leq_value< N0 >::value;	///< Force N to be power of 2
		static const size_t L = vtll::index_largest_bit< std::integral_constant<size_t, N> >::value - 1; ///< Index of largest bit in N
		static const size_t BIT_MASK = N - 1;	///< Bit mask to mask off lower bits to get index inside block

		using tuple_value_t = vtll::to_tuple<DATA>;	///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<DATA>; ///< Tuple holding refs to the entries	
		using tuple_const_ref_t = vtll::to_const_ref_tuple<DATA>; ///< Tuple holding refs to the entries

		using array_tuple_t1 = std::array<tuple_value_t, N>;///< ROW: an array of tuples
		using array_tuple_t2 = vtll::to_tuple<vtll::transform_size_t<DATA, std::array, N>>;	///< COLUMN: a tuple of arrays
		using block_t = std::conditional_t<ROW, array_tuple_t1, array_tuple_t2>; ///< Memory layout of the table

		using block_ptr_t = std::shared_ptr<block_t>; ///< Shared pointer to a block
		struct block_map_t {
			std::pmr::vector<std::atomic<block_ptr_t>> m_blocks;	///< Vector of shared pointers to the blocks
		};

	public:
		VlltStaticTable(size_t r = 1 << 16, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) noexcept
			: m_mr{ mr }, m_allocator{ mr }, m_block_map{ nullptr } {};

		~VlltStaticTable() noexcept {};

		inline auto size() noexcept -> size_t; ///< \returns the current numbers of rows in the table

		template<typename READ, typename WRITE>
		inline auto view() noexcept -> VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE> { 
			return VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE>(this); 
		};

		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(push_callback_t, Cs&&... data) -> table_index_t;

	protected:

		//-------------------------------------------------------------------------------------------
		//read data

		template<size_t I, typename C = vtll::Nth_type<DATA, I>>
		inline auto get_component_ptr(table_index_t n) noexcept -> C*;

		template<typename Ts>
		inline auto get_ref_tuple(table_index_t n) noexcept -> vtll::to_ref_tuple<Ts>;	///< \returns a tuple with refs to all components

		template<typename Ts>
		inline auto get_const_ref_tuple(table_index_t n) noexcept -> vtll::to_const_ref_tuple<Ts> { return get_ref_tuple<Ts>(n); };	///< \returns a tuple with refs to all components

		//-------------------------------------------------------------------------------------------
		//erase data

		inline auto pop_back() noexcept -> tuple_value_t; ///< Remove the last row, call destructor on components
		inline auto clear() noexcept -> size_t; ///< Set the number if rows to zero - effectively clear the table, call destructors
		inline auto swap(table_index_t n1, table_index_t n2) noexcept -> void;	///< Swap contents of two rows
		inline auto erase(table_index_t n1) -> tuple_value_t; ///< Remove a row, call destructor on components

		//-------------------------------------------------------------------------------------------
		//manage data

		inline auto max_size() noexcept -> size_t;
		static inline auto block_idx(table_index_t n) -> block_idx_t { return block_idx_t{ (n >> L) }; }
		inline auto resize(table_index_t slot) -> std::shared_ptr<block_map_t>;
		inline auto shrink() -> void;

		std::array<VlltSpinlock, vtll::size<DATA>::value> m_access_mutex;

		std::pmr::memory_resource* m_mr; ///< Memory resource for allocating blocks
		std::pmr::polymorphic_allocator<block_map_t> m_allocator;  ///< use this allocator
		alignas(64) std::atomic<std::shared_ptr<block_map_t>> m_block_map;///< Atomic shared ptr to the map of blocks

		using slot_size_t = vsty::strong_type_t<uint64_t, vsty::counter<>>;
		table_index_t table_size(slot_size_t size) { return table_index_t{ size.get_bits(0, NUMBITS1) }; }	
		table_diff_t  table_diff(slot_size_t size) { return table_diff_t{ size.get_bits_signed(NUMBITS1) }; }
		alignas(64) std::atomic<slot_size_t> m_size_cnt{ slot_size_t{ table_index_t{ 0 }, table_diff_t{0}, NUMBITS1 } };	///< Next slot and size as atomic
		alignas(64) std::atomic<uint64_t> m_starving{0}; ///< prevent one operation to starve the other: -1...pulls are starving 1...pushes are starving
	};


	/////
	// \brief Return number of rows when growing including new rows not yet established.
	// \returns number of rows when growing including new rows not yet established.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>

	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::max_size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::max(static_cast<decltype(table_size(size))>(table_size(size) + table_diff(size)), table_size(size));
	};

	/////
	// \brief Return number of valid rows.
	// \returns number of valid rows.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::min(static_cast<decltype(table_size(size))>(table_size(size) + table_diff(size)), table_size(size));
	};


	/// <summary>
	/// Return a pointer to a component.
	/// </summary>
	/// <typeparam name="C">Type of component.</typeparam>
	/// <typeparam name="I">Index in the component list.</typeparam>
	/// <param name="n">Slot number in the table.</param>
	/// <param name="map_ptr">Pointer to the table block holding the row.</param>
	/// <returns>Pointer to the component.</returns>
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	template<size_t I, typename C>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::get_component_ptr(table_index_t n) noexcept -> C* { ///< \returns a pointer to a component
		auto idx = block_idx(n);
		auto block_ptr = m_block_map.load()->m_blocks[idx].load(); ///< Access the block holding the slot
		if constexpr (ROW) { return &std::get<I>((*block_ptr)[n & BIT_MASK]); }
		else { return &std::get<I>(*block_ptr)[n & BIT_MASK]; }
	}


	/////
	// \brief Get a tuple with pointers to all components of an entry.
	// \param[in] n Index to the entry.
	// \returns a tuple with pointers to all components of entry n.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	template<typename Ts>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::get_ref_tuple(table_index_t n) noexcept -> vtll::to_ref_tuple<Ts> {
		return { [&] <size_t... Is>(std::index_sequence<Is...>) { 
			return std::tie(*get_component_ptr< vtll::index_of<DATA, vtll::Nth_type<Ts,Is>>::value >(table_index_t{n})...); 
		} (std::make_index_sequence<vtll::size<Ts>::value>{}) };
	};


	/// <summary>
	/// Insert a new row at the end of the table. Make sure that there are enough blocks to store the new data.
	/// If not allocate a new map to hold the segements, and allocate new blocks.
	/// </summary>
	/// <param name="n">Row number in the table.</param>
	/// <param name="first_seg">Index of first block that currently holds information.</param>
	/// <param name="last_seg">Index of the last block that currently holds informaton.</param>
	/// <param name="data">The data for the new row.</param>
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::push_back(push_callback_t callback, Cs&&... data) -> table_index_t {

		if constexpr (FAIR) {
			if( m_starving.load()==-1 ) m_starving.wait(-1); //wait until pushes are done and pulls have a chance to catch up
			if( stack_diff(m_size_cnt.load()) < -4 ) m_starving.store(1); //if pops are starving the pushes, then prevent pulls 
		}
		//increase size.m_diff to announce your demand for a new slot -> slot is now reserved for you
		slot_size_t size = m_size_cnt.load();	///< Make sure that no other thread is popping currently
		while (table_diff(size) < 0 || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_size(size), table_diff(size) + 1, NUMBITS1 } )) {
			if ( table_diff(size)  < 0 ) { //here compare_exchange_weak was NOT called to copy manually
				size = m_size_cnt.load();
			}
			//call wait function here
		};

		//make sure there is enough space in the block VECTOR - if not then change the old map to a larger map
		//VlltTable<DATA, N0, ROW, MINSLOTS>::push_back(table_index_t{stack_size(size) + stack_diff(size)}, std::forward<Cs>(data)...); ///< Make sure there are enough MINSLOTS for blocks

		auto n = (table_index_t{table_size(size) + table_diff(size)}); ///< Get the index of the new row
		auto map_ptr = resize(n); //if need be, grow the map of blocks

		//copy or move the data to the new slot, using a recursive templated lambda
		auto f = [&]<size_t I, typename T, typename... Ts>(auto && fun, T && dat, Ts&&... dats) {
			if constexpr (vtll::is_atomic<T>::value) get_component_ptr<I>(n)->store(dat); //copy value for atomic
			else *get_component_ptr<I>(n) = std::forward<T>(dat); //move or copy
			if constexpr (sizeof...(dats) > 0) { fun.template operator() < I + 1 > (fun, std::forward<Ts>(dats)...); } //recurse
		};
		f.template operator() < 0 > (f, std::forward<Cs>(data)...);

		if(callback.has_value()) callback.value()( table_index_t{ table_size(size) + table_diff(size) } ); ///< Call callback function

		slot_size_t new_size = slot_size_t{ table_size(size), table_diff(size) + 1, NUMBITS1 };	///< Increase size to validate the new row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ table_size(new_size) + 1, table_diff(new_size) - 1, NUMBITS1 } ));
		
		if constexpr (FAIR) {
			if(table_diff(new_size) - 1 == 0) { 
				m_starving.store(0); //allow pushes again
				m_starving.notify_all(); //notify all waiting threads
			}
		}
		
		return table_index_t{ table_size(size) + table_diff(size) };	///< Return index of new entry
	}



	/// <summary>
	/// If the map of blocks is too small, allocate a larger one and copy the previous block pointers into it.
	/// Then make one CAS attempt. If the attempt succeeds, then remember the new block map.
	/// If the CAS fails because another thread beat us, then CAS will copy the new pointer so we can use it.
	/// </summary>
	/// <param name="slot">Slot number in the table.</param>
	/// <returns>Pointer to the block map.</returns>
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::resize(table_index_t slot) -> std::shared_ptr<block_map_t> {

		//Get a pointer to the block map. If there is none, then allocate a new one.
		auto map_ptr{ m_block_map.load() };
		if (!map_ptr) {
			auto new_map_ptr = std::make_shared<block_map_t>( //map has always as many MINSLOTS as its capacity is -> size==capacity
				block_map_t{ std::pmr::vector<std::atomic<block_ptr_t>>{MINSLOTS, m_mr} } //create a new map
			);
			m_block_map.compare_exchange_strong(map_ptr, new_map_ptr); //try to exchange old block map with new
			map_ptr = m_block_map.load();
		}

		//Make sure that there is enough space in the block map so that blocks are there to hold the new slot.
		//Because other threads might also do this, we need to run in a loop until we are sure that the new slot is covered.
		auto idx = block_idx(slot);

		while(1) {

			static VlltSpinlock spinlock;

			if ( idx < map_ptr->m_blocks.size() ) {	//test if the block is already there
				auto ptr = map_ptr->m_blocks[idx].load();
				if( ptr ) return map_ptr;	  //yes -> return

				spinlock.lock();
				ptr = map_ptr->m_blocks[idx].load();
				if( ptr ) return map_ptr;	  //yes -> return
				map_ptr->m_blocks[idx] = std::make_shared<block_t>(); //no -> get a new block
				spinlock.unlock();
				return map_ptr;
			}

			spinlock.lock(); //another thread might beat us here, so we need to check again after the lock
			map_ptr =  m_block_map.load();
			if( idx < map_ptr->m_blocks.size() ) {
				spinlock.unlock();
				continue;	//another thread increased the size of the map, but the block might not be there, so test again
			}

			//Allocate a new block map and populate it with empty semgement pointers.
			auto num_blocks = map_ptr->m_blocks.size();
			auto new_map_ptr = std::make_shared<block_map_t>( //map has always as many slots as its capacity is -> size==capacity
				block_map_t{ std::pmr::vector<std::atomic<block_ptr_t>>{num_blocks << 2, m_mr} } //increase existing one
			);

			//Copy the old block pointers into the new map. Create also empty blocks for the new slots (or get them from the cache).
			size_t idx = 0;
			std::ranges::for_each( map_ptr->m_blocks.begin(), map_ptr->m_blocks.end(), 
				[&](auto& ptr) {
					auto block_ptr = ptr.load();
					if( block_ptr ) new_map_ptr->m_blocks[idx].store( block_ptr ); //copy
					else {
						auto new_block = std::make_shared<block_t>(); //no -> get a new block
						if( map_ptr->m_blocks[idx].compare_exchange_strong( block_ptr, new_block ) ) { 
							new_map_ptr->m_blocks[idx].store( new_block );
						} else {
							//m_block_cache.push(new_block); //another thread beat us, so we can use the new block
							new_map_ptr->m_blocks[idx].store( block_ptr ); //use block frm other thread
						}
					}
					++idx;
				}
			);

			map_ptr = new_map_ptr; ///<  remember for later	
			m_block_map.store( map_ptr );
			spinlock.unlock();
		}
		
		return map_ptr;
	}

	/// <summary>
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::shrink() -> void {
		
	}


	/////
	// \brief Pop the last row if there is one.
	// \param[in] tup Pointer to tuple to move the row data into.
	// \param[in] del If true, then call desctructor on the removed slot.
	// \returns true if a row was popped.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::pop_back() noexcept -> tuple_value_t {
	vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		if constexpr (FAIR) {
			if( m_starving.load()==1 ) m_starving.wait(1); //wait until pulls are done and pushes have a chance to catch up
			if( table_diff(m_size_cnt.load()) > 4 ) m_starving.store(-1); //if pushes are starving the pulls, then prevent pushes
		}

		slot_size_t size = m_size_cnt.load();

		/// Make sure that no other thread is currently pushing a new row
		while (table_diff(size) > 0 || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_size(size), table_diff(size) - 1, NUMBITS1 })) {
			if (table_diff(size) > 0) { size = m_size_cnt.load(); }
		};

		auto map_ptr{ m_block_map.load() };						///< Access the block map

		auto idx = table_size(size) + table_diff(size) - 1;
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(* (this->template get_component_ptr<i>(table_index_t{ idx })) ); }	//move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *(this->template get_component_ptr<i>(table_index_t{ idx }, map_ptr)); }				//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = this->template get_component_ptr<i>(table_index_t{ idx }, map_ptr)->load(); } 		//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { this->template get_component_ptr<i>(table_index_t{ idx })->~type(); }	///< Call destructor
			}
		);

		shrink(); ///< Shrink the block map if necessary

		slot_size_t new_size = slot_size_t{ table_size(size), table_diff(size) - 1, NUMBITS1 };	///< Commit the popping of the row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ table_size(new_size) - 1, table_diff(new_size) + 1, NUMBITS1 }));

		if constexpr (FAIR) {
			if(table_diff(new_size) + 1 == 0) { 
				m_starving.store(0); //allow pushes again
				m_starving.notify_all(); //notify all waiting threads
			}
		}	
		
		return ret; //RVO?
	}


	/////
	// \brief Pop all rows and call the destructors.
	// \returns number of popped rows.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::clear() noexcept -> size_t {
		size_t num = size();
		for( size_t i = 0; i < size(); ++i ) { pop_back(); }
		return num;
	}


	/////
	// \brief Swap the values of two rows.
	// \param[in] n1 Index of first row.
	// \param[in] n2 Index of second row.
	// \returns true if the operation was successful.
	///
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::swap(table_index_t idst, table_index_t isrc) noexcept -> void {
		assert(idst < size() && isrc < size());
		auto src = get_ref_tuple<DATA>(isrc);
		if (!src.has_value()) return;
		auto dst = get_ref_tuple<DATA>(idst);
		if (!dst.has_value()) return;
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >([&](auto i) {
			using type = vtll::Nth_type<DATA, i>;
			//std::cout << typeid(type).name() << "\n";
			if constexpr (std::is_move_assignable_v<type> && std::is_move_constructible_v<type>) {
				std::swap(std::get<i>(dst.value()), std::get<i>(src.value()));
			}
			else if constexpr (std::is_copy_assignable_v<type> && std::is_copy_constructible_v<type>) {
				auto& tmp{ std::get<i>(src.value()) };
				std::get<i>(src.value()) = std::get<i>(dst.value());
				std::get<i>(dst.value()) = tmp;
			}
			else if constexpr (vtll::is_atomic<type>::value) {
				type tmp{ std::get<i>(src.value()).load() };
				std::get<i>(src.value()).store(std::get<i>(dst.value()).load());
				std::get<i>(dst.value()).store(tmp.load());
			}
		});
		return;
	}


	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::erase(table_index_t n1) -> tuple_value_t {
		auto n2 = size() - 1;
		if (n1 == n2) return pop_back();
		swap(n1, table_index_t{ n2 });
		return pop_back();
	}


	//---------------------------------------------------------------------------------------------------


	/// <summary>
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
		//requires VlltStaticTableViewConcept<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE>
	class VlltStaticTableView {

		static const bool OWNER = (vtll::has_all_types<DATA, WRITE>::value && vtll::has_all_types<WRITE, DATA>::value);

		VlltStaticTableView(VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>* table ) : m_table{ table } {	
			if constexpr (SYNC == VLLT_SYNC_EXTERNAL) return;

			vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
				[&](auto i) {
					if constexpr ( vtll::size<READ>::value >0 && vtll::has_type<READ,vtll::Nth_type<DATA,i>>::value ) { 
						if constexpr (SYNC == VLLT_SYNC_DEBUG) assert(m_table->m_access_mutex[i].try_shared_lock());
						else m_table->m_access_mutex[i].shared_lock(); 
					}
					else if constexpr ( vtll::size<WRITE>::value >0 &&vtll::has_type<WRITE,vtll::Nth_type<DATA,i>>::value) { 
						if constexpr (SYNC == VLLT_SYNC_DEBUG) assert(m_table->m_access_mutex[i].try_lock());
						else m_table->m_access_mutex[i].lock(); 
					}
				}
			);
		};	
		
		using tuple_value_t = vtll::to_tuple<DATA>;	///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<WRITE>; ///< Tuple holding refs to the entries
		using tuple_const_ref_t = vtll::to_const_ref_tuple<READ>; ///< Tuple holding refs to the entries
		
		using tuple_return_t = vtll::to_tuple< vtll::cat< vtll::to_const_ref<READ>, vtll::to_ref<WRITE> > >;

		friend class VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>;

	public:
		~VlltStaticTableView() {
			if constexpr (SYNC == VLLT_SYNC_EXTERNAL) return;

			vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
				[&](auto i) {
					if constexpr ( vtll::has_type<READ,vtll::Nth_type<DATA,i>>::value ) { m_table->m_access_mutex[i].shared_unlock(); }
					else if constexpr ( vtll::has_type<WRITE,vtll::Nth_type<DATA,i>>::value) { m_table->m_access_mutex[i].unlock(); }
				}
			);
		};

		VlltStaticTableView(VlltStaticTableView& other) = delete;
		VlltStaticTableView& operator=(VlltStaticTableView& other) = delete;
		VlltStaticTableView(VlltStaticTableView&& other) = delete;
		VlltStaticTableView& operator=(VlltStaticTableView&& other) = delete;
	
		inline auto get(table_index_t n) -> std::optional< tuple_return_t > {
			if(n >= m_table->size()) return std::nullopt;
			return std::tuple_cat( m_table->template get_const_ref_tuple<READ>(n), m_table->template get_ref_tuple<WRITE>(n) ); 
		};

		inline auto pop_back() noexcept -> std::optional< tuple_value_t > requires OWNER {
			if(m_table->size()==0) return std::nullopt;
			return { m_table->pop_back() }; 
		}; 

		inline auto clear() noexcept -> size_t requires OWNER { return m_table->clear(); };
		inline auto swap(table_index_t other) noexcept -> void requires OWNER { m_table->swap(m_n, other); };	
		
		inline auto erase(table_index_t n) -> std::optional< tuple_value_t > requires OWNER {
			if(n >= m_table->size()) return std::nullopt;
			return { m_table->erase(n) }; 
		}

	private:
		VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>* m_table;
	};





	//---------------------------------------------------------------------------------------------------

	//---------------------------------------------------------------------------------------------------





	//---------------------------------------------------------------------------------------------------



}


