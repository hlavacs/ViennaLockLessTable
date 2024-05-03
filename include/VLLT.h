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
#include <thread>
#include <latch>
#include <numeric>
#include <string>
#include <cstdlib>
#include <random>
#include <functional>
#include <cstddef>  // for std::ptrdiff_t
#include <iterator> // for std::random_access_iterator_tag
#include <compare>
#include <memory>
#include <mutex>
#include <typeinfo>
#include <typeindex>

#include "VTLL.h"
#include "VSTY.h"


/// \brief 
namespace vllt {


	//---------------------------------------------------------------------------------------------------

	using table_index_t = vsty::strong_type_t<uint64_t, vsty::counter<>, std::integral_constant<uint64_t, std::numeric_limits<uint64_t>::max()>>;///< Strong integer type for indexing rows, 0 to number rows - 1
	using table_diff_t  = vsty::strong_type_t<int64_t, vsty::counter<>, std::integral_constant<int64_t, std::numeric_limits<int64_t>::max()>>;
	auto operator+(table_index_t lhs, table_diff_t rhs) { return table_index_t{ lhs.value() + rhs.value() }; }

	//---------------------------------------------------------------------------------------------------

	/// Syncronization type for the table
	const int VLLT_SYNC_PUSHBACK = 128;		///< relaxed sync, views with pushback are allowed
	enum class sync_t {
		VLLT_SYNC_EXTERNAL = 0,		///< sync is done externally
		VLLT_SYNC_EXTERNAL_PUSHBACK = VLLT_SYNC_EXTERNAL | VLLT_SYNC_PUSHBACK,	///< sync is done externally
		VLLT_SYNC_INTERNAL = 1,		///< full internal sync
		VLLT_SYNC_INTERNAL_PUSHBACK = VLLT_SYNC_INTERNAL | VLLT_SYNC_PUSHBACK,	///< relaxed internal sync, can add rows in parallel through the table
		VLLT_SYNC_DEBUG = 3,		///< debugging full internal sync - error if violation
		VLLT_SYNC_DEBUG_PUSHBACK = VLLT_SYNC_DEBUG | VLLT_SYNC_PUSHBACK	///< debugging relaxed internal sync - error if violation
	};

	/// Tag for template parameter list to indicate that the view has write access
	struct VlltWrite {};		///< Types before this tag have read access, types after this tag have write access

	/// Concept demanding that types of a table must be unique
	template<typename DATA>
	concept VlltStaticTableConcept = vtll::unique<DATA>::value;

	/// Forward declaration of VlltStaticTable
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR>
		requires VlltStaticTableConcept<DATA>
	class VlltStaticTable;

	/// Concept for view. Types must be unique, and no type must be in both READ and WRITE
	/// Currently defunct because of compiler error
	template<typename DATA, typename READ, typename WRITE>
	concept VlltStaticTableViewConcept = (
		VlltStaticTableConcept<DATA> 
		&& (vtll::size< vtll::intersection< vtll::tl<READ, WRITE>> >::value == 0) 
		&& (vtll::has_all_types<DATA, READ>::value) 
		&& (vtll::has_all_types<DATA, WRITE>::value)
	);

	/// Used for accessing a table.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
	class VlltStaticTableView;

	// A TABLE that satisfies this concept is ALLOWED to CREATE pushback-only views.
	template<sync_t SYNC>
	concept VlltAllowOnlyPushback = (((int)SYNC & VLLT_SYNC_PUSHBACK) != 0);

	//a VIEW that satisfies this concept has write access to all columns of the table is the owner and can add, pop, erase, and change anything
	template<typename DATA, typename WRITE>
	concept VlltWriteAll = (vtll::has_all_types<DATA, WRITE>::value && vtll::has_all_types<WRITE, DATA>::value); ///< Is the view the owner of the table?

	// A VIEW that satisfies this concept is a push-back only view, i.e., it can only add rows to the table but nothing else,
	// even though it is also an owner.
	template<typename DATA, sync_t SYNC, typename READ, typename WRITE>
	concept VlltOnlyPushback = (vtll::size<READ>::value == 1 && std::is_same_v< vtll::front<READ>, VlltWrite >); ///< Is the view only allowed to push back?

	template<typename DATA, sync_t SYNC, typename READ, typename WRITE>
	concept VlltOwner = (VlltWriteAll<DATA, WRITE> && !VlltOnlyPushback<DATA, SYNC, READ, WRITE>);

	/// Iterator forward declaration
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
	class VtllStaticIterator;

	/// Stack forward declaration
	template<typename T, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR>
	class VlltStack;

	/// VlltStaticTable is the base class for some classes, enabling management of tables that can be appended in parallel.
	/// \tparam DATA Types of the table.
	/// \tparam SYNC Synchronization type for the table.
	/// \tparam N0 Number of rows in a block.
	/// \tparam ROW If true, then the table is row based, otherwise column based.
	/// \tparam MINSLOTS Minimum number of slots in a block.
	/// \tparam FAIR If true, then the table is fair, otherwise not.
	template<typename DATA, sync_t SYNC = sync_t::VLLT_SYNC_EXTERNAL, size_t N0 = 1 << 5, bool ROW = false, size_t MINSLOTS = 16, bool FAIR = false>
		requires VlltStaticTableConcept<DATA>
	class VlltStaticTable {

	public:
		template<typename U1, sync_t U2, size_t U3, bool U4, size_t U5, bool U6, typename U7, typename U8>
		friend class VlltStaticTableView;

		template<typename U1, sync_t U2, size_t U3, bool U4, size_t U5, bool U6, typename U7, typename U8>
		friend class VlltStaticIterator;

		using tuple_value_t = vtll::to_tuple<DATA>;	///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<DATA>; ///< Tuple holding refs to the entries	
		using tuple_const_ref_t = vtll::to_const_ref_tuple<DATA>; ///< Tuple holding refs to the entries

	protected:
		static_assert(std::is_default_constructible_v<DATA>, "Your components are not default constructible!");

		const size_t NUMBITS1 = 44; ///< Number of bits for the index of the first item in the stack
		using block_idx_t = vsty::strong_type_t<uint64_t, vsty::counter<>>; ///< Strong integer type for indexing blocks, 0 to size map - 1

		static const size_t N = vtll::smallest_pow2_leq_value< N0 >::value;	///< Force N to be power of 2
		static const size_t L = vtll::index_largest_bit< std::integral_constant<size_t, N> >::value - 1; ///< Index of largest bit in N
		static const size_t BIT_MASK = N - 1;	///< Bit mask to mask off lower bits to get index inside block

		using array_tuple_t1 = std::array<tuple_value_t, N>;///< ROW: an array of tuples
		using array_tuple_t2 = vtll::to_tuple<vtll::transform_size_t<DATA, std::array, N>>;	///< COLUMN: a tuple of arrays
		using block_t = std::conditional_t<ROW, array_tuple_t1, array_tuple_t2>; ///< Memory layout of the table

		using block_ptr_t = std::shared_ptr<block_t>; ///< Shared pointer to a block
		struct block_map_t {
			std::pmr::vector<std::atomic<block_ptr_t>> m_blocks;	///< Vector of shared pointers to the blocks
		};

		using slot_size_t = vsty::strong_type_t<uint64_t, vsty::counter<>> ;
		using size_cnt_t1 = vsty::strong_type_t<slot_size_t, vsty::counter<>> ;
		using size_cnt_t2 = std::atomic<slot_size_t>;
		using size_cnt_t = std::conditional_t< SYNC == sync_t::VLLT_SYNC_EXTERNAL, size_cnt_t1, size_cnt_t2 >; ///< Atomic size counter
		using starving_t = std::atomic<uint64_t>; ///< Indicator for starving, use only for stack

	public:
		/// \brief Constructor of class VlltStaticTable
		/// \param pmr Memory resource for allocating blocks
		VlltStaticTable(std::pmr::memory_resource* pmr = std::pmr::new_delete_resource()) noexcept
			: m_alloc{ pmr }, m_block_map{ nullptr } {};

		/// Return the number of rows in the table.
		/// \returns The number of rows in the table.
		inline auto size() noexcept {
			auto size = m_size_cnt.load();
			auto s1 = table_index_t{ table_size(size) + table_diff(size) };
			auto s2 = table_size(size);
			return std::min(s1, s2);
 		}

		/// Return a view to the table.
		/// \tparam ...Ts Types of the table the view accesses.
		/// \returns a view to the table.
		template<typename... Ts >
		inline auto view() noexcept;

		/// Return a view to the table that writes to all types.
		template<>
		inline auto view<>() noexcept { return VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, vtll::tl<>, DATA>(*this); };

		friend bool operator==(const VlltStaticTable& lhs, const VlltStaticTable& rhs) noexcept { return &lhs == &rhs; }

	protected:

		/// \brief Add a new row to the table.
		/// \param data Data for the new row.
		/// \returns Index of the new row.
		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back_p( Cs&&... data ) noexcept -> table_index_t;
 
		//-------------------------------------------------------------------------------------------
		//read data

		template<size_t I, typename C = vtll::Nth_type<DATA, I>>  ///< Return a pointer to the component
		inline auto get_component_ptr(block_ptr_t block_ptr, table_index_t n) noexcept -> C* {
			if constexpr (ROW) { return &std::get<I>((*block_ptr)[n & BIT_MASK]); }
			else { return &std::get<I>(*block_ptr)[n & BIT_MASK]; }
		}

		template<typename Ts>
		inline auto get_ref_tuple(table_index_t n) noexcept -> vtll::to_ref_tuple<Ts>;	///< \returns a tuple with refs to all components

		template<typename Ts>
		inline auto get_const_ref_tuple(table_index_t n) noexcept -> vtll::to_const_ref_tuple<Ts> { return get_ref_tuple<Ts>(n); };	///< \returns a tuple with refs to all components

		//-------------------------------------------------------------------------------------------
		//erase data

		inline auto pop_back(table_index_t* idx = nullptr) noexcept -> tuple_value_t; ///< Remove the last row, call destructor on components
		inline auto clear() noexcept; ///< Set the number if rows to zero - effectively clear the table, call destructors
		inline auto swap(auto src, auto dst) noexcept -> void;	///< Swap contents of two rows
		inline auto swap(table_index_t isrc, table_index_t idst) noexcept -> void {swap( get_ref_tuple<DATA>(isrc), get_ref_tuple<DATA>(idst) );}	///< Swap contents of two rows
		inline auto erase(table_index_t n1) -> tuple_value_t; ///< Remove a row, call destructor on components

		//-------------------------------------------------------------------------------------------
		//manage data

		inline auto max_size() noexcept -> size_t {
			auto size = m_size_cnt.load();
			return std::max(static_cast<decltype(table_size(size))>(table_size(size) + table_diff(size)), table_size(size));
		}

		static inline auto block_idx(table_index_t n) -> block_idx_t { return block_idx_t{ (n.value() >> L) }; }
		inline auto resize(table_index_t slot) -> block_ptr_t; ///< If the map of blocks is too small, allocate a larger one and copy the previous block pointers into it.

		std::array<std::shared_timed_mutex, vtll::size<DATA>::value> m_access_mutex;
		std::pmr::polymorphic_allocator<block_t> m_alloc; ///< Allocator for the table

		alignas(64) std::atomic<std::shared_ptr<block_map_t>> m_block_map{nullptr};///< Atomic shared ptr to the map of blocks

		table_index_t table_size(slot_size_t size) { return table_index_t{ size.get_bits(0, NUMBITS1) }; }	
		table_diff_t  table_diff(slot_size_t size) { return table_diff_t{ (int64_t)size.get_bits_signed(NUMBITS1) }; }
		alignas(64) size_cnt_t m_size_cnt{ slot_size_t{ table_index_t{ 0 }, table_diff_t{0}, NUMBITS1 } };	///< Next slot and size as atomic
		alignas(64) std::atomic<uint64_t> m_starving{0}; ///< prevent one operation to starve the other: -1...pulls are starving 1...pushes are starving
	};


	/// \brief Create a view to the table.
	/// \returns a view to the table.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	template<typename... Ts >
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::view() noexcept {
		using parameters = vtll::tl<Ts...>;		///< List of types in the view

		if constexpr (sizeof...(Ts) == 1 && std::is_same_v<vtll::front<parameters>, VlltWrite>) {
			static_assert(VlltAllowOnlyPushback<SYNC>, "This table's SYNC option does not allow pushback-only views!");
			return VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, vtll::tl<VlltWrite>, DATA>(*this); ///< Create a view
		} else {
			static const size_t write = vtll::index_of<parameters, VlltWrite>::value; 		///< Index of VlltWrite in the view
			static const bool write_valid = (write != std::numeric_limits<size_t>::max()); 	///< Is VlltWrite in the view?

			using read_list1 = typename std::conditional< sizeof...(Ts) == 0 || (write_valid && write == 0), vtll::tl<>, vtll::sublist<parameters, 0, write> >::type;
			using read_list = vtll::remove_types< read_list1, vtll::tl<VlltWrite> >; //cannot use write - 1 if write == 0!

			using write_list = typename std::conditional< sizeof...(Ts) == 0 	//if no types are given
				|| !write_valid, vtll::tl<>, vtll::sublist<parameters, write + 1, sizeof...(Ts) - 1> >::type; //list of types with write access

			return VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, read_list, write_list>(*this); ///< Create a view
		}
	}


	/// \brief Get a tuple with pointers to all components of an entry.
	/// \param[in] n Index to the entry.
	/// \returns a tuple with pointers to all components of entry n.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	template<typename Ts>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::get_ref_tuple(table_index_t n) noexcept -> vtll::to_ref_tuple<Ts> {
		auto block_ptr = m_block_map.load()->m_blocks[(size_t)block_idx(n)].load();
		return { [&] <size_t... Is>(std::index_sequence<Is...>) { 
			return std::tie(*get_component_ptr< vtll::index_of<DATA, vtll::Nth_type<Ts,Is>>::value >(block_ptr, table_index_t{n})...); 
		} (std::make_index_sequence<vtll::size<Ts>::value>{}) };
	};


	/// Insert a new row at the end of the table. Make sure that there are enough blocks to store the new data.
	/// If not allocate a new map to hold the segements, and allocate new blocks.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::push_back_p(Cs&&... data) noexcept -> table_index_t {
		if constexpr (FAIR) {
			if( m_starving.load()==-1 ) m_starving.wait(-1); //wait until pushes are done and pulls have a chance to catch up
			if( table_diff(m_size_cnt.load()) < -4 ) m_starving.store(1); //if pops are starving the pushes, then prevent pulls 
		}
		
		//increase size.m_diff to announce your demand for a new slot -> slot is now reserved for you
		slot_size_t size = m_size_cnt.load();	///< Make sure that no other thread is popping currently
		while (table_diff(size) < 0 || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_size(size), table_diff(size) + 1, NUMBITS1 } )) {
			if ( table_diff(size)  < 0 ) { //here compare_exchange_weak was NOT called to copy manually
				size = m_size_cnt.load();
			}
			//call wait() here
		};

		auto n = (table_index_t{table_size(size) + table_diff(size)}); ///< Get the index of the new row
		auto block_ptr = resize(n); //if need be, grow the map of blocks

		//copy or move the data to the new slot, using a recursive templated lambda
		auto f = [&]<size_t I, typename T, typename... Ts>(auto && fun, T && dat, Ts&&... dats) {
			if constexpr (vtll::is_atomic<T>::value) get_component_ptr<I>(block_ptr, n)->store(dat); //copy value for atomic
			else *get_component_ptr<I>(block_ptr, n) = std::forward<T>(dat); //move or copy
			if constexpr (sizeof...(dats) > 0) { fun.template operator() < I + 1 > (fun, std::forward<Ts>(dats)...); } //recurse
		};
		f.template operator() < 0 > (f, std::forward<Cs>(data)...);

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


	/// \brief If the map of blocks is too small, allocate a larger one and copy the previous block pointers into it.
	/// Then make one CAS attempt. If the attempt succeeds, then remember the new block map.
	/// If the CAS fails because another thread beat us, then CAS will copy the new pointer so we can use it.
	/// \param[in] slot Slot number in the table.
	/// \returnss Pointer to the block map.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::resize(table_index_t slot) -> block_ptr_t {
		static std::mutex m;

		//Get a pointer to the block map. If there is none, then allocate a new one.
		auto map_ptr{ m_block_map.load() };
		if (!map_ptr) {
			std::scoped_lock lock(m);
			map_ptr = m_block_map.load();
			if( !map_ptr ) {
				map_ptr = std::allocate_shared<block_map_t>( //map has always as many MINSLOTS as its capacity is -> size==capacity
					m_alloc, block_map_t{ std::pmr::vector<std::atomic<block_ptr_t>>{MINSLOTS, m_alloc} } //create a new map
				);
				m_block_map.store( map_ptr );
			}
		}

		assert(map_ptr != nullptr); ///< Make sure that the block map is there

		//Make sure that there is enough space in the block map so that blocks are there to hold the new slot.
		//Because other threads might also do this, we need to run in a loop until we are sure that the new slot is covered.
		auto idx = block_idx(slot);

		while(1) {
			if ( idx < map_ptr->m_blocks.size() ) {	//test if the block is already there
				auto ptr = map_ptr->m_blocks[(size_t)idx].load();
				if( ptr ) return ptr;	  //yes -> return

				std::scoped_lock lock(m);
				ptr = map_ptr->m_blocks[(size_t)idx].load();
				if( ptr ) return ptr;	  //yes -> return
				map_ptr->m_blocks[(size_t)idx].store( std::allocate_shared<block_t>(m_alloc) ); //no -> get a new block
				return map_ptr->m_blocks[(size_t)idx].load();
			}

			std::scoped_lock lock(m);

			map_ptr =  m_block_map.load();
			if( idx < map_ptr->m_blocks.size() ) {
				continue;	//another thread increased the size of the map, but the block might not be there, so test again
			}

			//Allocate a new block map and populate it with empty semgement pointers.
			auto num_blocks = map_ptr->m_blocks.size();
			auto new_size = num_blocks << 2; //double the size of the map
			while( idx >= new_size ) new_size <<= 2; //make sure there are enough slots for the new block

			auto new_map_ptr = std::allocate_shared<block_map_t>( //map has always as many slots as its capacity is -> size==capacity
				m_alloc, block_map_t{ std::pmr::vector<std::atomic<block_ptr_t>>{new_size, m_alloc} } //increase existing one
			);

			//Copy the old block pointers into the new map. 
			for( size_t i = 0; i < num_blocks; ++i ) {
				auto ptr = map_ptr->m_blocks[i].load();
				if( ptr ) new_map_ptr->m_blocks[i].store( ptr );
				else new_map_ptr->m_blocks[i].store( std::allocate_shared<block_t>(m_alloc) ); //get a new block
			}
			for( size_t i = num_blocks; i <= idx; ++i ) {
				new_map_ptr->m_blocks[i].store( std::allocate_shared<block_t>(m_alloc) ); //get a new block
			}

			map_ptr = new_map_ptr; ///<  remember for later	
			m_block_map.store( map_ptr );
		}
	}



	/// \brief Pop the last row if there is one.
	/// \param[out] idx_ptr Index of the deleted row.
	/// \returnss values of the popped row.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::pop_back(table_index_t* idx_ptr) noexcept -> tuple_value_t {
	vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};
		table_index_t idx{};
		if(idx_ptr) *idx_ptr = idx; ///< Initialize the index to an invalid value

		if constexpr (FAIR) {
			if( m_starving.load()==1 ) m_starving.wait(1); //wait until pulls are done and pushes have a chance to catch up
			if( table_diff(m_size_cnt.load()) > 4 ) m_starving.store(-1); //if pushes are starving the pulls, then prevent pushes
		}

		slot_size_t size = m_size_cnt.load();
		if (table_size(size) + table_diff(size) == 0) return {};	///< Is there a row to pop off?

		/// Make sure that no other thread is currently pushing a new row
		while (table_diff(size) > 0 || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_size(size), table_diff(size) - 1, NUMBITS1 })) {
			if (table_diff(size) > 0) { size = m_size_cnt.load(); }
			if (table_size(size) + table_diff(size) == 0) return {};	///< Is there a row to pop off?
		};

		auto map_ptr{ m_block_map.load() };						///< Access the block map

		idx = table_size(size) + table_diff(size) - 1; 		///< Get the index of the row to pop
		if(idx_ptr) *idx_ptr = idx; ///< Store index of popped row as out value

		auto block_ptr = m_block_map.load()->m_blocks[(size_t)block_idx(idx)].load();
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(* (this->template get_component_ptr<i>(block_ptr, table_index_t{ idx })) ); }	//move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *(this->template get_component_ptr<i>(block_ptr, table_index_t{ idx }, map_ptr)); }		//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = this->template get_component_ptr<i>(block_ptr, table_index_t{ idx }, map_ptr)->load(); } 		//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { this->template get_component_ptr<i>(block_ptr, table_index_t{ idx })->~type(); }	///< Call destructor
			}
		);

		//shrink the table
		auto bidx = block_idx(table_size(size));
		if( bidx + 2 < map_ptr->m_blocks.size() ) {
			map_ptr->m_blocks[(size_t)bidx + 2].store(nullptr);
		}	

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


	/// \brief Pop all rows and call the destructors.
	/// \returnss number of popped rows.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::clear() noexcept {
		auto num = size();
		table_index_t idx;
		pop_back(&idx);
		while( idx.has_value()) { pop_back(&idx); }
		return num;
	}


	/// \brief Swap the values of two rows.
	/// \param[in] n1 Index of first row.
	/// \param[in] n2 Index of second row.
	/// \returnss true if the operation was successful.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::swap( auto src, auto dst ) noexcept -> void {
		if constexpr (std::is_same_v< decltype(src), table_index_t  >) assert(dst < size() && src < size());
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >([&](auto i) {
			using type = vtll::Nth_type<DATA, i>;
			if constexpr (std::is_move_assignable_v<type> && std::is_move_constructible_v<type>) {
				std::swap(std::get<i>(dst), std::get<i>(src));
			}
			else if constexpr (std::is_copy_assignable_v<type> && std::is_copy_constructible_v<type>) {
				auto& tmp{ std::get<i>(src) };
				std::get<i>(src) = std::get<i>(dst);
				std::get<i>(dst) = tmp;
			}
			else if constexpr (vtll::is_atomic<type>::value) {
				type tmp{ std::get<i>(src).load() };
				std::get<i>(src).store(std::get<i>(dst).load());
				std::get<i>(dst).store(tmp.load());
			}
		});
		return;
	}

	/// \brief Remove a row from the table.
	/// \param n1 Index of the row to remove.
	/// \returns Tuple holding the values of the removed row.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR> requires VlltStaticTableConcept<DATA>
	inline auto VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>::erase(table_index_t n1) -> tuple_value_t {
		table_index_t n2;
		auto ret = pop_back( &n2 );
		if (n1 == n2) return ret;
		swap( ret, get_ref_tuple<DATA>(n1)); 
		return ret;
	}


	//---------------------------------------------------------------------------------------------------
	//table view

	class VlltStaticTableViewBase {
	public:
		virtual auto get_ptr() -> std::vector<void*> { 
			return {};			
		}
		std::vector<std::type_index> m_types;
	};


	/// \brief VlltStaticTableView is a view to a VlltStaticTable. It allows to read and write to the table.
	/// \tparam DATA Types of the table.
	/// \tparam SYNC Synchronization type for the table.
	/// \tparam N0 Number of rows in a block.
	/// \tparam ROW If true, then the table is row based, otherwise column based.
	/// \tparam MINSLOTS Minimum number of slots in a block.
	/// \tparam FAIR If true, then the table is fair, otherwise not.
	/// \tparam READ Types that can be read from the table.
	/// \tparam WRITE Types that can be written to the table.
	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
	class VlltStaticTableView : public VlltStaticTableViewBase {

	public:
		using table_type = VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>; ///< Type of the table

		using tuple_value_t = table_type::tuple_value_t;	///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<WRITE>; ///< Tuple holding refs to the entries
		using tuple_const_ref_t = vtll::to_const_ref_tuple<READ>; ///< Tuple holding refs to the entries
		
		using tuple_return_t = vtll::to_tuple< vtll::cat< vtll::to_const_ref<READ>, vtll::to_ref<WRITE> > >; ///< Tuple holding refs to the entries

		friend class VlltStaticTable<DATA, SYNC, N0, ROW, MINSLOTS, FAIR>; ///< Allow the table to access the view

		/// \brief Constructor of class VlltStaticTableView. This is private because only the table is allowed to create a view.
		VlltStaticTableView(table_type& table ) : VlltStaticTableViewBase{}, m_table{ table } {	
			if constexpr (SYNC == sync_t::VLLT_SYNC_EXTERNAL || SYNC == sync_t::VLLT_SYNC_EXTERNAL_PUSHBACK) return;
			if constexpr (VlltOnlyPushback<DATA, SYNC, READ, WRITE>) return;

			vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
				[&](auto i) {
					m_types.emplace_back( typeid(vtll::Nth_type<DATA,i>) );
					if constexpr ( vtll::size<READ>::value >0 && vtll::has_type<READ,vtll::Nth_type<DATA,i>>::value ) { 
						if constexpr (SYNC == sync_t::VLLT_SYNC_DEBUG || SYNC == sync_t::VLLT_SYNC_DEBUG_PUSHBACK) assert(m_table.m_access_mutex[i].try_lock_shared());
						else m_table.m_access_mutex[i].lock_shared(); 
					}
					else if constexpr ( vtll::size<WRITE>::value >0 && vtll::has_type<WRITE,vtll::Nth_type<DATA,i>>::value) { 
						if constexpr (SYNC == sync_t::VLLT_SYNC_DEBUG || SYNC == sync_t::VLLT_SYNC_DEBUG_PUSHBACK) assert(m_table.m_access_mutex[i].try_lock());
						else m_table.m_access_mutex[i].lock(); 
					}
				}
			);
		};	
		

	public:
		/// \brief Destructor of class VlltStaticTableView
		~VlltStaticTableView() {
			if constexpr (SYNC == sync_t::VLLT_SYNC_EXTERNAL || SYNC == sync_t::VLLT_SYNC_EXTERNAL_PUSHBACK) return;
			if constexpr (VlltOnlyPushback<DATA, SYNC, READ, WRITE>) return;

			vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
				[&](auto i) {
					if constexpr ( vtll::has_type<READ,vtll::Nth_type<DATA,i>>::value ) { m_table.m_access_mutex[i].unlock_shared(); }
					else if constexpr ( vtll::has_type<WRITE,vtll::Nth_type<DATA,i>>::value) { m_table.m_access_mutex[i].unlock(); }
				}
			);
		};

		VlltStaticTableView(VlltStaticTableView& other) = delete; ///< Copy constructor is deleted
		VlltStaticTableView& operator=(VlltStaticTableView& other) = delete; ///< Copy assignment operator is deleted
		VlltStaticTableView(VlltStaticTableView&& other) = delete; ///< Move constructor is deleted
		VlltStaticTableView& operator=(VlltStaticTableView&& other) = delete; ///< Move assignment operator is deleted
	
		inline auto size() noexcept (!VlltOnlyPushback<DATA, SYNC, READ, WRITE>) { return m_table.size(); } ///< Return the number of rows in the table.

		/// \brief Add a new row to the table.
		/// \tparam ...Cs Types of the data to add.
		/// \param ...data Data to add.
		/// \returnss Index of the new row.
		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(Cs&&... data) -> table_index_t requires VlltWriteAll<DATA, WRITE> { 
			return m_table.push_back_p(std::forward<Cs>(data)...); 
		};

		/// \brief Get a tuple with refs to all components of an entry.
		/// \param n Index to the entry.
		/// \returnss a tuple with refs to all components of entry n.
		inline decltype(auto) get(table_index_t n) requires (!VlltOnlyPushback<DATA, SYNC, READ, WRITE>) {
			if constexpr (vtll::size<READ>::value == 0) return m_table.template get_ref_tuple<WRITE>(n);
			else if constexpr (vtll::size<WRITE>::value == 0) return m_table.template get_const_ref_tuple<READ>(n);
			else return std::tuple_cat( m_table.template get_const_ref_tuple<READ>(n), m_table.template get_ref_tuple<WRITE>(n) ); 
		};

		/// \brief Pop last row from the table.
		/// \returnss Tuple with the data of the last row.
		inline auto pop_back(table_index_t *idx = nullptr ) noexcept requires VlltOwner<DATA, SYNC, READ, WRITE> { return m_table.pop_back(idx); }; 

		/// \brief Clear the table.
		inline auto clear() noexcept requires VlltOwner<DATA, SYNC, READ, WRITE> { return m_table.clear(); };

		/// \brief Swap the values of two rows.
		inline auto swap(table_index_t lhs, table_index_t rhs) noexcept -> void requires VlltOwner<DATA, SYNC, READ, WRITE> { m_table.swap(lhs, rhs); };	
		
		/// \brief Erase a row from the table. Replace it with the last row. Return the values.
		inline auto erase(table_index_t n) -> tuple_value_t requires VlltOwner<DATA, SYNC, READ, WRITE> { return m_table.erase(n); }

		/// \brief Equality comparison operator
    	friend bool operator==(const VlltStaticTableView& lhs, const VlltStaticTableView& rhs) {
        	return lhs.m_table == rhs.m_table;
    	}

		/// \brief Create an iterator to the beginning of the table.
		/// \returns  Iterator to the beginning of the table.
		auto begin() requires (!VlltOnlyPushback<DATA, SYNC, READ, WRITE>) { return VtllStaticIterator<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE>(*this, table_index_t{0}); } 
		
		/// \brief Create an iterator to the end of the table.
		/// \returns  Iterator to the end of the table.
		auto end() requires (!VlltOnlyPushback<DATA, SYNC, READ, WRITE>) { return VtllStaticIterator<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE>(*this, table_index_t{size() - 1}); } 

	private:
		table_type& m_table; ///< Reference to the table
	};

	//---------------------------------------------------------------------------------------------------
	//table view iterator

	template<typename DATA, sync_t SYNC, size_t N0, bool ROW, size_t MINSLOTS, bool FAIR, typename READ, typename WRITE>
	class VtllStaticIterator {
	public:
		using view_type = VlltStaticTableView<DATA, SYNC, N0, ROW, MINSLOTS, FAIR, READ, WRITE>; ///< Type of the view	
    	using difference_type = table_diff_t; ///< Type of the difference between two iterators
		using value_type = vtll::to_tuple< vtll::cat< READ, WRITE > >; ///< Type of the value the iterator points to
   	 	using pointer = table_index_t; ///< Type of the pointer the iterator points to
    	using reference = vtll::to_tuple< vtll::cat< vtll::to_const_ref<READ>, vtll::to_ref<WRITE> > >; ///< Type of the reference the iterator points to

    	using iterator_category = std::random_access_iterator_tag ; ///< Type of the iterator category

		/// \brief Constructor of the iterator
		/// \param view View to the table
		/// \param n Index of the row the iterator points to
    	VtllStaticIterator(view_type& view , table_index_t n = table_index_t{}) : m_view{ view }, m_n{n} {}; 

    	/// \brief Constructor of the iterator
    	/// \param view  View to the table
    	VtllStaticIterator(VtllStaticIterator& view) : m_view{ &view }, m_n{view.m_n} {};

		/// \brief Copy assignment operator
    	VtllStaticIterator& operator=(VtllStaticIterator& rhs){ m_view = rhs.m_view; m_n = rhs.m_n; return *this; };
    	reference operator*() const { return m_view.get(m_n); } ///< Dereference operator
    	pointer operator->() const { return m_view.get(m_n); }  ///< Arrow operator
    	reference operator[](difference_type n) const { return m_view.get( m_n + n ); } ///< Subscript operator

    	VtllStaticIterator& operator++() 		{ ++m_n; return *this; } ///< Prefix increment operator
    	VtllStaticIterator operator++(int) 		{ VtllStaticIterator temp = *this; ++m_n; return temp; } ///< Postfix increment operator
    	VtllStaticIterator& operator--() 		{ --m_n; return *this; } ///< Prefix decrement operator
    	VtllStaticIterator operator--(int) 		{ VtllStaticIterator temp = *this; --m_n; return temp;} ///< Postfix decrement operator
    	VtllStaticIterator& operator+=(difference_type n) { m_n += n; return *this;} ///< Addition assignment operator
    	VtllStaticIterator& operator-=(difference_type n) { m_n -= n; return *this; } ///< Subtraction assignment operator

    	/// \brief Unequality comparison operator
    	/// \param rhs  Right hand side of the comparison
    	/// \returns  True if the iterators are not equal
    	auto operator!=(const VtllStaticIterator& rhs) {
			return m_view != rhs.m_view || m_n != rhs.m_n;
		}

		/// \brief Spaceship comparison operator
		/// \param rhs  Right hand side of the comparison
		/// \returns  Partial ordering of the iterators
    	std::partial_ordering operator<=>(const VtllStaticIterator& rhs) {
			if(m_view == rhs.m_view) return m_n <=> rhs.m_n;
			return std::partial_ordering::unordered;
		}

		/// \brief Spaceship comparison operator
		/// \param lhs First iterator
		/// \param rhs  Second iterator
		/// \returns  Partial ordering of the iterators
		friend std::partial_ordering operator<=>(const VtllStaticIterator& lhs, const VtllStaticIterator& rhs) {
			if(lhs.m_view == rhs.m_view) return lhs.m_n <=> rhs.m_n;
			return std::partial_ordering::unordered;
		}

	private:
		table_index_t m_n;
		view_type& m_view;
	};


	//---------------------------------------------------------------------------------------------------


	/// \brief VlltStack is a simple stack on top of a VlltStaticTable.
	/// \tparam T Type being stored in the stack.
	/// \tparam N0 SIze of blocks in the table.
	/// \tparam ROW Boolean if the table is row based or column based.
	/// \tparam MINSLOTS Minimum number of slots in a block.
	/// \tparam FAIR If true then the stack will try to balance the number of pushes and pops.
	/// \tparam SYNC In deug checks whether the stack is used concurrently with other views (which is not allowed).
	template<typename T, size_t N0 = 1 << 5, bool ROW = false, size_t MINSLOTS = 16, bool FAIR = false>
	class VlltStack {
		using tuple_value_t = vtll::to_tuple<vtll::tl<T>>;	///< Tuple holding the entries as value
		using table_type_t = VlltStaticTable<vtll::tl<T>, sync_t::VLLT_SYNC_EXTERNAL, N0, ROW, MINSLOTS, FAIR>;
		using view_type_t = VlltStaticTableView<vtll::tl<T>, sync_t::VLLT_SYNC_EXTERNAL, N0, ROW, MINSLOTS, FAIR, vtll::tl<>, vtll::tl<T>>;

	public:
		/// \brief Constructor of class VlltStaticStack
		/// \param table Reference to the table
		VlltStack(std::pmr::memory_resource* pmr = std::pmr::new_delete_resource() ) : m_table{ pmr } {};

		inline auto size() noexcept { return m_table.size(); } ///< Return the number of rows in the table.

		/// \brief Add a new row to the table.
		/// \tparam ...Cs Types of the data to add.
		/// \param ...data Data to add.
		/// \returns Index of the new row.
		inline auto push_back(T&& data) -> table_index_t { 
			return m_view.push_back(std::forward<T>(data)); 
		};

		/// Pop last row from the table.
		/// \returnss Tuple with the data of the last row.
		inline auto pop_back() noexcept -> std::optional< tuple_value_t > {
			if( size() == 0 ) return std::nullopt;
			table_index_t n2;
			auto ret = m_view.pop_back( &n2 );
			if( n2.has_value() ) return ret;
			return std::nullopt; 
		};

	private:
		table_type_t m_table; ///< the table used by the stack
		view_type_t m_view = m_table.view(); ///< view to the table
	};


	//---------------------------------------------------------------------------------------------------



	//---------------------------------------------------------------------------------------------------



}


