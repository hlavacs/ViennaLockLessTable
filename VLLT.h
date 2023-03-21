#pragma once

#include <assert.h>
#include <memory_resource>
#include <shared_mutex>
#include <optional>
#include <array>
#include <concepts>
#include <algorithm>
#include <type_traits>
#include "VTLL.h"
#include "IntType.h"

namespace vllt {

	/////
	// \brief VlltTable is the base class for some classes, enabling management of tables that can be
	// appended in parallel.
	//
	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, typename table_index_t = uint64_t>
	class VlltTable {
	public:
		static_assert(std::is_default_constructible_v<DATA>, "Your components are not default constructible!");

		static const size_t N = vtll::smallest_pow2_leq_value< N0 >::value;									///< Force N to be power of 2
		static const size_t L = vtll::index_largest_bit< std::integral_constant<size_t, N> >::value - 1;	///< Index of largest bit in N
		static const uint64_t BIT_MASK = N - 1;			///< Bit mask to mask off lower bits to get index inside segment

		using tuple_value_t = vtll::to_tuple<DATA>;		///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<DATA>;	///< Tuple holding ptrs to the entries

		using array_tuple_t1 = std::array<tuple_value_t, N>;								///< ROW: an array of tuples
		using array_tuple_t2 = vtll::to_tuple<vtll::transform_size_t<DATA, std::array, N>>;	///< COLUMN: a tuple of arrays
		using segment_t = std::conditional_t<ROW, array_tuple_t1, array_tuple_t2>;			///< Memory layout of the table

		using segment_ptr_t = std::shared_ptr<segment_t>;				///<Shared pointer to a segment
		struct seg_vector_t {
			std::pmr::vector<std::atomic<segment_ptr_t>> m_segments;	///<Vector of atomic shared pointers to the segments
			size_t m_offset = 0;										///<Segment offset for FIFO queue
		};

		VlltTable(size_t r = 1 << 16, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) noexcept 
			: m_mr{ mr }, m_allocator{ mr }, m_seg_vector{ nullptr } {};
		~VlltTable() noexcept {};

		/// <summary>
		/// Return a pointer to a component.
		/// </summary>
		/// <typeparam name="C">Type of component.</typeparam>
		/// <typeparam name="I">Index in the component list.</typeparam>
		/// <param name="n">Slot number in the table.</param>
		/// <returns>Pointer to the component.</returns>
		template<size_t I, typename C = vtll::Nth_type<DATA, I>>
		inline auto component_ptr(table_index_t n) noexcept -> C* {		///< \returns a pointer to a component
			auto vector_ptr{ m_seg_vector.load() };
			auto segment_ptr = (vector_ptr->m_segments[n >> L]).load();
			if constexpr (ROW) { return &std::get<I>((*segment_ptr)[n & BIT_MASK]); }
			else { return &std::get<I>(*segment_ptr)[n & BIT_MASK]; }
		}
		
		/// <summary>
		/// Make sure there are enough segments in the vector to store a new slot.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the vector.</param>
		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		void insert(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr, size_t first_seg, size_t last_seg, Cs&&... data ) {
			size_t num_seg = vector_ptr ? vector_ptr->m_segments.capacity() + vector_ptr->m_offset : 0;	///< Current max number of segments
			while (slot >= N * num_seg) {		///< Do we have enough?		
				auto new_vector_ptr = std::make_shared<seg_vector_t>(
					seg_vector_t{ std::pmr::vector<std::atomic<segment_ptr_t>>{std::max(num_seg * 2, 16ULL), m_mr }, 0 } //new segment vector
				);

				for (size_t i = 0; num_seg > 0 && i < last_seg - first_seg + 1; ++i) {
					new_vector_ptr->m_segments[i].store(vector_ptr->m_segments[first_seg + i].load()); ///< Copy segment pointers
				};

				if (m_seg_vector.compare_exchange_strong(vector_ptr, new_vector_ptr)) {	///< Try to exchange old segment vector with new
					vector_ptr = new_vector_ptr;										///< If success, remember for later
				} //Note: if we were beaten by other thread, then compare_exchange_strong itself puts the new value into vector_ptr
				num_seg = vector_ptr->m_segments.capacity() + vector_ptr->m_offset;		///< Current max number of segments
			} //another thread could have beaten us here, so go back and retry

			auto seg_num = (slot >> L) - vector_ptr->m_offset;				///< Index of segment we need
			auto seg_ptr = vector_ptr->m_segments[seg_num].load();			///< Does the segment exist yet? If yes, increases use count.
			if (!seg_ptr) {													///< If not, create one
				auto new_seg_ptr = std::make_shared<segment_t>();			///< Create a new segment
				vector_ptr->m_segments[seg_num].compare_exchange_strong(seg_ptr, new_seg_ptr);	///< Try to put it into seg vector, someone might beat us here
			}

			auto f = [&]<size_t I, typename T, typename... Ts>(auto && fun, T && dat, Ts&&... dats) {
				if constexpr (vtll::is_atomic<T>::value) component_ptr<I>(slot)->store(dat);	//copy value for atomic
				else *component_ptr<I>(slot) = std::forward<T>(dat);							//move or copy
				if constexpr (sizeof...(dats) > 0) { fun.template operator() < I + 1 > (fun, std::forward<Ts>(dats)...); } //recurse
			};
			f.template operator() < 0 > (f, std::forward<Cs>(data)...);
		}

	protected:
		std::pmr::memory_resource*						m_mr;					///< Memory resource for allocating segments
		std::pmr::polymorphic_allocator<seg_vector_t>	m_allocator;			///< use this allocator
		std::atomic<std::shared_ptr<seg_vector_t>>		m_seg_vector;			///< Atomic shared ptr to the vector of segments
	};


	//---------------------------------------------------------------------------------------------------


	/////
	// \brief VlltStack is a data container similar to std::vector, but with additional properties
	//
	// VlltStack has the following properties:
	// 1) It stores tuples of data
	// 2) The memory layout is cache-friendly and can be row-oriented or column-oriented.
	// 3) Lockless multithreaded access. It can grow - by calling push_back() - even when
	// used with multiple threads. This is achieved by storing data in segments,
	// which are accessed over via a std::vector of shared_ptr. New segments can simply be added to the
	// std::vector. Also the std::vector can seamlessly grow using CAS.
	// Is can also shrink when using multithreading by calling pop_back(). Again, no locks are used!
	//
	// The number of items S per segment must be a power of 2 : N = 2^L. This way, random access to row K is esily achieved
	// by first right shift K >> L to get the index of the segment pointer, then use K & (N-1) to get the index within
	// the segment.
	//
	///
	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, typename table_index_t = uint32_t>
	class VlltStack : public VlltTable<DATA, N0, ROW, table_index_t> {

	public:
		using VlltTable<DATA, N0, ROW, table_index_t>::N;
		using VlltTable<DATA, N0, ROW, table_index_t>::L;
		using VlltTable<DATA, N0, ROW, table_index_t>::m_mr;
		using VlltTable<DATA, N0, ROW, table_index_t>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, table_index_t>::component_ptr;
		using VlltTable<DATA, N0, ROW, table_index_t>::insert;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using typename VlltTable<DATA, N0, ROW, table_index_t>::tuple_ref_t;
		using typename VlltTable<DATA, N0, ROW, table_index_t>::segment_t;
		using typename VlltTable<DATA, N0, ROW, table_index_t>::segment_ptr_t;
		using typename VlltTable<DATA, N0, ROW, table_index_t>::seg_vector_t;

		VlltStack(size_t r = 1 << 16, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) noexcept;
		~VlltStack() noexcept;

		//-------------------------------------------------------------------------------------------
		//read data

		inline auto size() noexcept -> size_t; ///< \returns the current numbers of rows in the table

		template<size_t I, typename C = vtll::Nth_type<DATA, I>>
		inline auto get(table_index_t n) noexcept -> C&;		///< \returns a ref to a component

		template<typename C>									///< \returns a ref to a component
		inline auto get(table_index_t n) noexcept -> C& requires vtll::unique<DATA>::value;

		inline auto get_tuple(table_index_t n) noexcept	-> tuple_ref_t;	///< \returns a tuple with refs to all components

		//-------------------------------------------------------------------------------------------
		//add data

		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(Cs&&... data) noexcept -> table_index_t;	///< Push new component data to the end of the table

		//-------------------------------------------------------------------------------------------
		//remove and swap data

		inline auto pop_back() noexcept	-> tuple_opt_t;	///< Remove the last row, call destructor on components
		inline auto clear() noexcept	-> size_t;	///< Set the number if rows to zero - effectively clear the table, call destructors
		inline auto compress() noexcept	-> void;	///< Deallocate unused segments
		inline auto swap(table_index_t n1, table_index_t n2) noexcept -> void;	///< Swap contents of two rows

	protected:

		struct slot_size_t {
			uint32_t m_next_slot{ 0 };	//index of next free slot
			uint32_t m_size{ 0 };		//number of valid entries
		};

		std::atomic<slot_size_t> m_size_cnt{ {0,0} };	///< Next slot and size as atomic

		inline auto max_size() noexcept -> size_t;
	};

	/////
	// \brief Constructor of class VlltStack.
	// \param[in] r Max number of rows that can be stored in the table.
	// \param[in] mr Memory allocator.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline VlltStack<DATA, N0, ROW, table_index_t>::VlltStack(size_t r, std::pmr::memory_resource* mr) noexcept : VlltTable<DATA, N0, ROW, table_index_t>(r, mr) {};

	/////
	// \brief Destructor of class VlltStack.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline VlltStack<DATA, N0, ROW, table_index_t>::~VlltStack() noexcept { clear(); };

	/////
	// \brief Return number of rows when growing including new rows not yet established.
	// \returns number of rows when growing including new rows not yet established.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::max_size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::max(size.m_next_slot, size.m_size);
	};

	/////
	// \brief Return number of valid rows.
	// \returns number of valid rows.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::min(size.m_next_slot, size.m_size);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	template<size_t I, typename C>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::get(table_index_t n) noexcept -> C& {
		assert(n < size());
		return *component_ptr<I, C>(n);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	template<typename C>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::get(table_index_t n) noexcept -> C& requires vtll::unique<DATA>::value {
		return get<vtll::index_of<DATA, C>::value>(n);
	};

	/////
	// \brief Get a tuple with pointers to all components of an entry.
	// \param[in] n Index to the entry.
	// \returns a tuple with pointers to all components of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::get_tuple(table_index_t n) noexcept -> tuple_ref_t {
		auto f = [&]<size_t... Is>(std::index_sequence<Is...>) { return std::tie(get<Is>(n)...); };
		return f(std::make_index_sequence<vtll::size<DATA>::value>{});
	};

	/////
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::push_back(Cs&&... data) noexcept	-> table_index_t {
		//increase m_next_slot to announce your demand for a new slot -> slot is now reserved for you
		slot_size_t size = m_size_cnt.load();	///< Make sure that no other thread is popping currently
		while (size.m_next_slot < size.m_size || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ size.m_next_slot + 1, size.m_size })) {
			if (size.m_next_slot < size.m_size) { //here compare_exchange_weak was NOT called to copy manually
				size = m_size_cnt.load();
			}
		};

		//make sure there is enough space in the segment VECTOR - if not then change the old vector to a larger vector
		auto vector_ptr{ m_seg_vector.load() };	///< Shared pointer to current segment ptr vector, can be nullptr
		insert(size.m_next_slot, vector_ptr, 0, vector_ptr ? vector_ptr->m_segments.size() - 1 : 0, std::forward<Cs>(data)...); ///< Make sure there are enough slots for segments

		slot_size_t new_size = m_size_cnt.load();	///< Increase size to validate the new row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_slot, new_size.m_size + 1 }));

		return table_index_t{ size.m_next_slot };	///< Return index of new entry
	}

	/////
	// \brief Pop the last row if there is one.
	// \param[in] tup Pointer to tuple to move the row data into.
	// \param[in] del If true, then call desctructor on the removed slot.
	// \returns true if a row was popped.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::pop_back() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		slot_size_t size = m_size_cnt.load();
		if (size.m_next_slot == 0) return std::nullopt;	///< Is there a row to pop off?

		/// Make sure that no other thread is currently pushing a new row
		while (size.m_next_slot > size.m_size || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ size.m_next_slot - 1, size.m_size })) {
			if (size.m_next_slot > size.m_size) { size = m_size_cnt.load(); }
			if (size.m_next_slot == 0) return std::nullopt;	///< Is there a row to pop off?
		};

		auto idx = size.m_next_slot - 1;
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(idx)); }	//move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(idx); }				//copy
				else if constexpr (vtll::is_atomic<type>::value)    { std::get<i>(ret) = component_ptr<i>(idx)->load(); } 		//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(idx)->~type(); }	///< Call destructor
			}
		);

		slot_size_t new_size = m_size_cnt.load();	///< Commit the popping of the row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_slot, new_size.m_size - 1 }));

		return ret; //RVO?
	}

	/////
	// \brief Pop all rows and call the destructors.
	// \returns number of popped rows.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::clear() noexcept -> size_t {
		size_t num = 0;
		while (pop_back().has_value()) { ++num; }
		return num;
	}

	/////
	// \brief Swap the values of two rows.
	// \param[in] n1 Index of first row.
	// \param[in] n2 Index of second row.
	// \returns true if the operation was successful.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::swap(table_index_t idst, table_index_t isrc) noexcept -> void {
		assert(idst < size() && isrc < size());
		auto src = get_tuple(isrc);
		auto dst = get_tuple(idst);
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >([&](auto i) {
			using type = vtll::Nth_type<DATA, i>;
			//std::cout << typeid(type).name() << "\n";
			if constexpr (std::is_move_assignable_v<type> && std::is_move_constructible_v<type>) {
				std::swap(std::get<i>(dst), std::get<i>(src));
			}
			else if constexpr (std::is_copy_assignable_v<type> && std::is_copy_constructible_v<type>) {
				type tmp{ std::get<i>(src) };
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

	/////
	// \brief Deallocate segments that are currently not used.
	// No parallel processing allowed when calling this function.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltStack<DATA, N0, ROW, table_index_t>::compress() noexcept -> void {
		auto vector_ptr{ VlltTable<DATA, N0, ROW, table_index_t>::m_seg_vector.load() };
		if (!vector_ptr) return;
		for (size_t i = vector_ptr->size() - 1; i > (max_size() >> L); --i) {
			auto seg_ptr = (*vector_ptr)[i].load();
			if (seg_ptr && seg_ptr.use_count() == 2) {
				std::shared_ptr<segment_t> new_seg_ptr;
				(*vector_ptr)[i].compare_exchange_strong(seg_ptr, new_seg_ptr);	///< Try to put it into seg vector, someone might beat us here
			}
		}
	}


	//----------------------------------------------------------------------------------------------------

	
	///
	// \brief VlltFIFOQueue is a FIFO queue that can be ued by multiple threads in parallel
	//
	// It has the following properties:
	// 1) It stores tuples of data
	// 2) Lockless multithreaded access.
	//
	// The FIFO queue is a stack thet keeps segment pointers in a vector.
	// Segments that are empty are recycled to the end of the segments vector.
	// An offset is maintained that is subtracted from a table index.
	//
	//

	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, typename table_index_t = uint64_t>
	class VlltFIFOQueue : public VlltTable<DATA, N0, ROW, table_index_t> {
	protected:
		using VlltTable<DATA, N0, ROW, table_index_t>::N;
		using VlltTable<DATA, N0, ROW, table_index_t>::L;
		using VlltTable<DATA, N0, ROW, table_index_t>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, table_index_t>::m_mr;
		using VlltTable<DATA, N0, ROW, table_index_t>::component_ptr;
		using VlltTable<DATA, N0, ROW, table_index_t>::insert;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using segment_t		= VlltTable<DATA, N0, ROW, table_index_t>::segment_t;
		using segment_ptr_t = VlltTable<DATA, N0, ROW, table_index_t>::segment_ptr_t;
		using seg_vector_t	= VlltTable<DATA, N0, ROW, table_index_t>::seg_vector_t;

		std::atomic<table_index_t>	m_first;		//next element to be taken out of the queue
		std::atomic<table_index_t>	m_consumed;		//last element that was taken out and fully read and destroyed
		std::atomic<table_index_t>	m_next_slot;	//next element to write over
		std::atomic<table_index_t>	m_last;			//last element that has been produced and fully constructed

	public:
		VlltFIFOQueue() {};

		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(Cs&&... data) noexcept -> table_index_t;	///< Push new component data to the end of the table

		inline auto pop_front() noexcept -> tuple_opt_t;	///< Remove the last row, call destructor on components
		inline auto clear() noexcept	 -> size_t;			///< Set the number if rows to zero - effectively clear the table, call destructors
	};


	///
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltFIFOQueue<DATA, N0, ROW, table_index_t>::push_back(Cs&&... data) noexcept -> table_index_t {
		auto next_slot = m_next_slot.fetch_add(1);			///< Slot number to put the new data into
		auto vector_ptr{ m_seg_vector.load() };				///< Shared pointer to current segment ptr vector, can be nullptr

		insert(	next_slot, vector_ptr, vector_ptr ? (m_first.load() >> L) - vector_ptr->m_offset : 0
				, vector_ptr ? (m_last.load() >> L) - vector_ptr->m_offset : 0, std::forward<Cs>(data)...);

		auto new_size = next_slot;
		while (!m_last.compare_exchange_weak(new_size, next_slot));	///< Increase size to validate the new row

		return table_index_t{ next_slot };	///< Return index of new entry
	}

	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltFIFOQueue<DATA, N0, ROW, table_index_t>::pop_front() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		auto next_slot = m_first.fetch_add(1);
		if (next_slot == m_next_slot) return std::nullopt;	///< the queue is empty

		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(next_slot)); } //move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(next_slot); } 			//copy
				else if constexpr (vtll::is_atomic<type>::value)    { std::get<i>(ret) = component_ptr<i>(next_slot)->load(); } 	//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(next_slot)->~type(); }	///< Call destructor
			}
		);

		auto committed = next_slot > 0 ? next_slot - 1 : 0;		
		while (!m_consumed.compare_exchange_weak(committed, next_slot)) {
			committed = next_slot > 0 ? next_slot - 1 : 0;
		}
		
		return ret;		//RVO?
	}

	///
	template<typename DATA, size_t N0, bool ROW, typename table_index_t>
	inline auto VlltFIFOQueue<DATA, N0, ROW, table_index_t>::clear() noexcept -> size_t {
		size_t num = 0;
		while (pop_front().has_value()) { ++num; }
		return num;
	}


}


