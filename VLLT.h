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
#include "VSTY.h"


namespace vllt {


	/////
	// \brief VlltTable is the base class for some classes, enabling management of tables that can be
	// appended in parallel.
	//
	using table_index_t = vsty::strong_type_null_t<size_t, vsty::counter<>, std::numeric_limits<size_t>::max()>;
	using segment_idx_t = vsty::strong_integral_t<size_t, vsty::counter<>>; ///<strong integer type for indexing segments, 0 to size vector-1

	template<typename T>
	T null_value() {
		return std::numeric_limits<T>::max();
	}

	template<typename T>
	bool has_value(T value) {
		return value.has_value(); // value != null_value<T>();
	}

	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltTable {
	protected:
		static_assert(std::is_default_constructible_v<DATA>, "Your components are not default constructible!");

		static const size_t N = vtll::smallest_pow2_leq_value< N0 >::value;									///< Force N to be power of 2
		static const size_t L = vtll::index_largest_bit< std::integral_constant<size_t, N> >::value - 1;	///< Index of largest bit in N
		static const uint64_t BIT_MASK = N - 1;			///< Bit mask to mask off lower bits to get index inside segment

		using tuple_value_t = vtll::to_tuple<DATA>;		///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<DATA>;	///< Tuple holding ptrs to the entries

		using array_tuple_t1 = std::array<tuple_value_t, N>;								///< ROW: an array of tuples
		using array_tuple_t2 = vtll::to_tuple<vtll::transform_size_t<DATA, std::array, N>>;	///< COLUMN: a tuple of arrays
		using segment_t = std::conditional_t<ROW, array_tuple_t1, array_tuple_t2>;			///< Memory layout of the table

		using segment_ptr_t = std::shared_ptr<segment_t>;						///<Shared pointer to a segment
		struct seg_vector_t {
			std::pmr::vector<std::atomic<segment_ptr_t>> m_segments;	///<Vector of atomic shared pointers to the segments
			segment_idx_t m_seg_offset = 0;								///<Segment offset for FIFO queue (offsets segments NOT rows)
		};

	public:
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
		/*template<size_t I, typename C = vtll::Nth_type<DATA, I>>
		inline auto component_ptr(table_index_t n, std::shared_ptr<seg_vector_t>& vector_ptr) noexcept -> C* {		///< \returns a pointer to a component
			auto seg = n >> L;
			auto idx = segment(n, vector_ptr);
			auto segment_ptr = (vector_ptr->m_segments[idx]).load();	///< Access the segment holding the slot
			if constexpr (ROW) { return &std::get<I>((*segment_ptr)[n & BIT_MASK]); }
			else { return &std::get<I>(*segment_ptr)[n & BIT_MASK]; }
		}*/

		template<size_t I, typename C = vtll::Nth_type<DATA, I>>
		inline auto component_ptr(table_index_t n, std::shared_ptr<seg_vector_t>& vector_ptr) noexcept -> C* {		///< \returns a pointer to a component
			auto seg = n >> L;
			auto idx = segment(n, vector_ptr);
			auto segment_ptr = (vector_ptr->m_segments[idx]).load();	///< Access the segment holding the slot
			if constexpr (ROW) { return &std::get<I>((*segment_ptr)[n & BIT_MASK]); }
			else { return &std::get<I>(*segment_ptr)[n & BIT_MASK]; }
		}

		/// <summary>
		/// Insert a new row at the end of the table. Make sure that there are enough segments to store the new data.
		/// If not allocate a new vector to hold the segements, and allocate new segments.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the segment vector.</param>
		/// <param name="first_seg">Index of first segment that currently holds information.</param>
		/// <param name="last_seg">Index of the last segment that currently holds informaton.</param>
		/// <param name="data">The data for the new row.</param>
		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		void insert(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr, std::atomic<table_index_t>& first_slot, Cs&&... data) {
			resize(slot, vector_ptr, first_slot); //if need be, grow the vector of segments

			//copy or move the data to the new slot, using a recursive templated lambda
			auto f = [&]<size_t I, typename T, typename... Ts>(auto && fun, T && dat, Ts&&... dats) {
				if constexpr (vtll::is_atomic<T>::value) component_ptr<I>(slot, vector_ptr)->store(dat);	//copy value for atomic
				else *component_ptr<I>(slot, vector_ptr) = std::forward<T>(dat);							//move or copy
				if constexpr (sizeof...(dats) > 0) { fun.template operator() < I + 1 > (fun, std::forward<Ts>(dats)...); } //recurse
			};
			f.template operator() < 0 > (f, std::forward<Cs>(data)...);
		}

	protected:

		/// <summary>
		/// Return the segment index for a given slot.
		/// </summary>
		/// <param name="n">Slot.</param>
		/// <param name="offset">Segment offset to be used.</param>
		/// <returns>Index of the segment for the slot.</returns>
		static auto segment(table_index_t n, size_t offset) {
			return segment_idx_t{ (n >> L) - offset };
		}

		/// <summary>
		/// Return the segment index for a given slot.
		/// </summary>
		/// <param name="n">Slot.</param>
		/// <param name="vector_ptr">Pointer to the vector of segments, and offset</param>
		/// <returns>Index of the segment for the slot.</returns>
		static auto segment(table_index_t n, std::shared_ptr<seg_vector_t>& vector_ptr) {
			return segment(n, vector_ptr->m_seg_offset );
		}

		/// <summary>
		/// If the vector of segments is too small, allocate a larger one and copy the previous segment pointers into it.
		/// Then make one CAS attempt. If the attempt succeeds, then remember the new segment vector.
		/// If the CAS fails because another thread beat us, then CAS will copy the new pointer so we can use it.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the segment vector.</param>
		/// <param name="first_seg">Index of first segment that currently holds information.</param>
		/// <returns></returns>
		inline auto resize(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr, std::atomic<table_index_t>& first_slot) {
			if (!vector_ptr) {
				auto new_vector_ptr = std::make_shared<seg_vector_t>( //vector has always as many slots as its capacity is -> size==capacity
					seg_vector_t{ std::pmr::vector<std::atomic<segment_ptr_t>>{SLOTS, m_mr}, segment_idx_t{0} } //increase existing one
				);
				for (auto& ptr : new_vector_ptr->m_segments) {
					ptr = std::make_shared<segment_t>();
				}
				if (m_seg_vector.compare_exchange_strong(vector_ptr, new_vector_ptr)) {	///< Try to exchange old segment vector with new
					vector_ptr = new_vector_ptr;										///< If success, remember for later
				} //Note: if we were beaten by other thread, then compare_exchange_strong itself puts the new value into vector_ptr
			}

			while ( slot >= N * (vector_ptr->m_segments.size() + vector_ptr->m_seg_offset) ) {
				segment_idx_t first_seg{ 0 };
				auto fs = first_slot.load();
				if (has_value(fs)) {
					first_seg = segment(fs, vector_ptr);
				}
				size_t num_segments = vector_ptr->m_segments.size();
				size_t new_offset = vector_ptr->m_seg_offset + first_seg;
				
				size_t min_size = segment(slot, new_offset);
				size_t smaller_size = std::max((num_segments >> 1), SLOTS);
				size_t new_size = 2 * num_segments;
				while (min_size > new_size) { new_size *= 2; }

				if (first_seg > num_segments * 0.8 && min_size < smaller_size) new_size = smaller_size;
				else if (first_seg > (num_segments >> 1) && min_size < num_segments) new_size = num_segments;

				auto new_vector_ptr = std::make_shared<seg_vector_t>( //vector has always as many slots as its capacity is -> size==capacity
					seg_vector_t{ std::pmr::vector<std::atomic<segment_ptr_t>>{new_size, m_mr}, segment_idx_t{new_offset} } //increase existing one
				);
				
				size_t idx = 0;
				std::ranges::for_each(new_vector_ptr->m_segments.begin(), new_vector_ptr->m_segments.end(), [&](auto& ptr) {
					if (first_seg + idx < num_segments) { ptr.store( vector_ptr->m_segments[first_seg + idx].load() ); }
					else {
						size_t i1 = idx - (num_segments - first_seg);
						if (i1 < first_seg) { ptr.store(vector_ptr->m_segments[i1].load()); }
						else { ptr = std::make_shared<segment_t>();	}
					}
					++idx;
				});

				if (m_seg_vector.compare_exchange_strong(vector_ptr, new_vector_ptr)) {	///< Try to exchange old segment vector with new
					vector_ptr = new_vector_ptr;										///< If success, remember for later
				} //Note: if we were beaten by other thread, then compare_exchange_strong itself puts the new value into vector_ptr
			}
		}

		std::pmr::memory_resource* m_mr;					///< Memory resource for allocating segments
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
	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltStack : public VlltTable<DATA, N0, ROW, SLOTS> {

	public:

		using VlltTable<DATA, N0, ROW, SLOTS>::N;
		using VlltTable<DATA, N0, ROW, SLOTS>::L;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_mr;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, SLOTS>::component_ptr;
		using VlltTable<DATA, N0, ROW, SLOTS>::insert;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::tuple_ref_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_ptr_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::seg_vector_t;
		//using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_idx_t;

		VlltStack(size_t r = 1 << 16, std::pmr::memory_resource* mr = std::pmr::new_delete_resource()) noexcept;
		~VlltStack() noexcept;

		//-------------------------------------------------------------------------------------------
		//read data

		inline auto size() noexcept -> size_t; ///< \returns the current numbers of rows in the table

		template<size_t I>
		inline auto get(table_index_t n) noexcept -> typename vtll::Nth_type<DATA, I>&;		///< \returns a ref to a component

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
			table_index_t m_next_free_slot{ 0 };	//index of next free slot
			table_index_t m_size{ 0 };				//number of valid entries
		};

		std::atomic<slot_size_t> m_size_cnt{ {table_index_t{0}, table_index_t{ 0 }} };	///< Next slot and size as atomic

		inline auto max_size() noexcept -> size_t;
	};

	/////
	// \brief Constructor of class VlltStack.
	// \param[in] r Max number of rows that can be stored in the table.
	// \param[in] mr Memory allocator.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline VlltStack<DATA, N0, ROW, SLOTS>::VlltStack(size_t r, std::pmr::memory_resource* mr) noexcept : VlltTable<DATA, N0, ROW, SLOTS>(r, mr) {};

	/////
	// \brief Destructor of class VlltStack.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline VlltStack<DATA, N0, ROW, SLOTS>::~VlltStack() noexcept { clear(); };

	/////
	// \brief Return number of rows when growing including new rows not yet established.
	// \returns number of rows when growing including new rows not yet established.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::max_size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::max(size.m_next_free_slot, size.m_size);
	};

	/////
	// \brief Return number of valid rows.
	// \returns number of valid rows.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::min(size.m_next_free_slot, size.m_size);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<size_t I>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get(table_index_t n) noexcept -> typename vtll::Nth_type<DATA, I>& {
		assert(n < size());
		auto vector_ptr = m_seg_vector.load();
		return *component_ptr<I>(n, vector_ptr);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename C>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get(table_index_t n) noexcept -> C& requires vtll::unique<DATA>::value {
		return get<vtll::index_of<DATA, C>::value>(n);
	};

	/////
	// \brief Get a tuple with pointers to all components of an entry.
	// \param[in] n Index to the entry.
	// \returns a tuple with pointers to all components of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get_tuple(table_index_t n) noexcept -> tuple_ref_t {
		return[&]<size_t... Is>(std::index_sequence<Is...>) { return std::tie(get<Is>(n)...); }(std::make_index_sequence<vtll::size<DATA>::value>{});
	};

	/////
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::push_back(Cs&&... data) noexcept -> table_index_t {
		//increase m_next_free_slot to announce your demand for a new slot -> slot is now reserved for you
		slot_size_t size = m_size_cnt.load();	///< Make sure that no other thread is popping currently
		while (size.m_next_free_slot < size.m_size || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_index_t{ size.m_next_free_slot + 1 }, size.m_size })) {
			if (size.m_next_free_slot < size.m_size) { //here compare_exchange_weak was NOT called to copy manually
				size = m_size_cnt.load();
			}
		};

		//make sure there is enough space in the segment VECTOR - if not then change the old vector to a larger vector
		auto vector_ptr{ m_seg_vector.load() };	///< Shared pointer to current segment ptr vector, can be nullptr
		//insert(size.m_next_free_slot, vector_ptr, segment_idx_t{ 0 }, std::forward<Cs>(data)...); ///< Make sure there are enough slots for segments

		slot_size_t new_size = m_size_cnt.load();	///< Increase size to validate the new row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_free_slot, table_index_t{ new_size.m_size + 1} }));

		return table_index_t{ size.m_next_free_slot };	///< Return index of new entry
	}

	/////
	// \brief Pop the last row if there is one.
	// \param[in] tup Pointer to tuple to move the row data into.
	// \param[in] del If true, then call desctructor on the removed slot.
	// \returns true if a row was popped.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::pop_back() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		slot_size_t size = m_size_cnt.load();
		if (size.m_next_free_slot == 0) return std::nullopt;	///< Is there a row to pop off?

		/// Make sure that no other thread is currently pushing a new row
		while (size.m_next_free_slot > size.m_size ||
			!m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_index_t{size.m_next_free_slot - 1}, size.m_size })) {

			if (size.m_next_free_slot > size.m_size) { size = m_size_cnt.load(); }
			if (size.m_next_free_slot == 0) return std::nullopt;	///< Is there a row to pop off?
		};

		auto vector_ptr{ m_seg_vector.load() };						///< Access the segment vector

		auto idx = size.m_next_free_slot - 1;
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(table_index_t{ idx }, vector_ptr)); }	//move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(table_index_t{ idx }, vector_ptr); }				//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = component_ptr<i>(table_index_t{ idx }, vector_ptr)->load(); } 		//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(table_index_t{ idx }, vector_ptr)->~type(); }	///< Call destructor
			}
		);

		slot_size_t new_size = m_size_cnt.load();	///< Commit the popping of the row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_free_slot, table_index_t{ new_size.m_size - 1} }));

		return ret; //RVO?
	}

	/////
	// \brief Pop all rows and call the destructors.
	// \returns number of popped rows.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::clear() noexcept -> size_t {
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
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::swap(table_index_t idst, table_index_t isrc) noexcept -> void {
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
	// Is lockless and threadsafe.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::compress() noexcept -> void {
		auto vector_ptr{ m_seg_vector.load() };
		if (!vector_ptr) return;
		for (size_t i = vector_ptr->m_segments.size() - 1; i > (max_size() >> L); --i) {
			auto seg_ptr = vector_ptr->m_segments[i].load();
			if (seg_ptr) {
				if (seg_ptr.use_count() == 2) {
					std::shared_ptr<segment_t> new_seg_ptr;
					if (!vector_ptr->m_segments[i].compare_exchange_strong(seg_ptr, new_seg_ptr))	///< Try to put it into seg vector, someone might beat us here
						return;
				}
				else return;
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

	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltFIFOQueue : public VlltTable<DATA, N0, ROW, SLOTS> {

	public:
		using VlltTable<DATA, N0, ROW, SLOTS>::N;
		using VlltTable<DATA, N0, ROW, SLOTS>::L;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_mr;
		using VlltTable<DATA, N0, ROW, SLOTS>::component_ptr;
		using VlltTable<DATA, N0, ROW, SLOTS>::insert;
		using VlltTable<DATA, N0, ROW, SLOTS>::segment;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_ptr_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::seg_vector_t;

		VlltFIFOQueue() {};

		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(Cs&&... data) noexcept -> table_index_t;	///< Push new component data to the end of the table

		inline auto pop_front() noexcept -> tuple_opt_t;	///< Remove the last row, call destructor on components
		inline auto size() noexcept		 -> size_t;				///< Number of elements in the queue
		inline auto clear() noexcept	 -> size_t;			///< Set the number if rows to zero - effectively clear the table, call destructors

	protected:
		std::atomic<table_index_t>	m_next{ table_index_t{0} };	//next element to be taken out of the queue
		std::atomic<table_index_t>	m_consumed{null_value<table_index_t>()};		//last element that was taken out and fully read and destroyed
		std::atomic<table_index_t>	m_next_free_slot{ table_index_t{0} };	//next element to write over
		std::atomic<table_index_t>	m_last{null_value<table_index_t>()};			//last element that has been produced and fully constructed
	};


	///
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::push_back(Cs&&... data) noexcept -> table_index_t {
		auto next_free_slot = m_next_free_slot.load();
		while (!m_next_free_slot.compare_exchange_weak(next_free_slot, table_index_t{ next_free_slot + 1 }));///< Slot number to put the new data into	

		auto vector_ptr{ m_seg_vector.load() };		///< Shared pointer to current segment ptr vector, can be nullptr
		insert(next_free_slot, vector_ptr, m_consumed, std::forward<Cs>(data)...);

		table_index_t old_last;		///< Increase size to validate the new row
		do {			
			old_last = next_free_slot > 0 ? table_index_t{ next_free_slot - 1 } : table_index_t{null_value<table_index_t>()};
		} while (! m_last.compare_exchange_weak(old_last, next_free_slot));

		return next_free_slot;	///< Return index of new entry
	}

	/// <summary>
	/// Pop the next item from the queue.
	/// Move its content into the return value (tuple),
	/// then remove it from the queue.
	/// </summary>
	/// <typeparam name="DATA">Type list of data items.</typeparam>
	/// <typeparam name="N0">Number of items per segment.</typeparam>
	/// <typeparam name="ROW">ROW or COLUMN layout.</typeparam>
	/// <typeparam name="SLOTS">Default number of slots in the first segment vector.</typeparam>
	/// <returns>Tuple with values from the next item, or nullopt.</returns>
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::pop_front() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		if (!has_value(m_last.load())) return std::nullopt;

		table_index_t next, last, p1;
		do {
			next = m_next.load();
			last = m_last.load();
			if (!(next <= last)) return std::nullopt;
		} while (!m_next.compare_exchange_weak(next, table_index_t{next + 1ul}));  ///< Slot number to put the new data into	
		
		auto vector_ptr{ m_seg_vector.load() };						///< Access the segment vector

		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(next, vector_ptr)); } //move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(next, vector_ptr); } 			//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = component_ptr<i>(next, vector_ptr)->load(); } 	//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(next, vector_ptr)->~type(); }	///< Call destructor
			}
		);

		table_index_t consumed;
		do {
			consumed = (next > 0 ? table_index_t{ next - 1ull } : table_index_t{null_value<table_index_t>()});
		} while (!m_consumed.compare_exchange_weak(consumed, next));

		return ret;		//RVO?
	}

	/// <summary>
	/// Get the number of items currently in the queue.
	/// </summary>
	/// <typeparam name="DATA">Type list of data items.</typeparam>
	/// <typeparam name="N0">Number of items per segment.</typeparam>
	/// <typeparam name="ROW">ROW or COLUMN layout.</typeparam>
	/// <typeparam name="SLOTS">Default number of slots in the first segment vector.</typeparam>
	/// <returns>Number of items currently in the queue.</returns>
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::size() noexcept -> size_t {
		auto last = m_last.load();
		auto consumed = m_consumed.load();
		size_t sz{0};
		if (has_value(last)) {	//have items been produced yet?
			sz += last;			//yes -> count them
			if (has_value(consumed)) sz -= consumed; //have items been consumed yet?
			else ++sz;	//no -> we start at zero, so increase by 1
		}

		return sz;
	}

	/// <summary>
	/// Remove all items from the queue.
	/// </summary>
	/// <typeparam name="DATA">Type list of data items.</typeparam>
	/// <typeparam name="N0">Number of items per segment.</typeparam>
	/// <typeparam name="ROW">ROW or COLUMN layout.</typeparam>
	/// <typeparam name="SLOTS">Default number of slots in the first segment vector.</typeparam>
	/// <returns>Number of items removed from the queue.</returns>
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::clear() noexcept -> size_t {
		size_t num = 0;
		while (pop_front().has_value()) { ++num; }
		return num;
	}

}





/*

namespace vllt {

	/////
	// \brief VlltTable is the base class for some classes, enabling management of tables that can be
	// appended in parallel.
	//
	using table_index_t = vsty::strong_type_null_t<size_t, vsty::counter<>, std::numeric_limits<size_t>::max()>;

	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltTable {
	protected:
		static_assert(std::is_default_constructible_v<DATA>, "Your components are not default constructible!");

		static const size_t N = vtll::smallest_pow2_leq_value< N0 >::value;									///< Force N to be power of 2
		static const size_t L = vtll::index_largest_bit< std::integral_constant<size_t, N> >::value - 1;	///< Index of largest bit in N
		static const uint64_t BIT_MASK = N - 1;			///< Bit mask to mask off lower bits to get index inside segment

		using tuple_value_t = vtll::to_tuple<DATA>;		///< Tuple holding the entries as value
		using tuple_ref_t = vtll::to_ref_tuple<DATA>;	///< Tuple holding ptrs to the entries

		using array_tuple_t1 = std::array<tuple_value_t, N>;								///< ROW: an array of tuples
		using array_tuple_t2 = vtll::to_tuple<vtll::transform_size_t<DATA, std::array, N>>;	///< COLUMN: a tuple of arrays
		using segment_t = std::conditional_t<ROW, array_tuple_t1, array_tuple_t2>;			///< Memory layout of the table

		using segment_ptr_t = std::shared_ptr<segment_t>;						///<Shared pointer to a segment
		using segment_idx_t = vsty::strong_integral_t<size_t, vsty::counter<>>; ///<strong integer type for indexing segments, 0 to size vector-1
		struct seg_vector_t {
			std::pmr::vector<std::atomic<segment_ptr_t>> m_segments;	///<Vector of atomic shared pointers to the segments
			segment_idx_t m_seg_offset = 0;								///<Segment offset for FIFO queue (offsets segments NOT rows)
		};

	public:
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
			auto vector_ptr{ m_seg_vector.load() };						///< Access the segment vector

			auto idx = segment(n, vector_ptr); 
			assert(vector_ptr->m_segments[idx].load());

			auto segment_ptr = (vector_ptr->m_segments[idx]).load();	///< Access the segment holding the slot
			if constexpr (ROW) { return &std::get<I>((*segment_ptr)[n & BIT_MASK]); }
			else { return &std::get<I>(*segment_ptr)[n & BIT_MASK]; }
		}
		
		/// <summary>
		/// Insert a new row at the end of the table. Make sure that there are enough segments to store the new data.
		/// If not allocate a new vector to hold the segements, and allocate new segments.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the segment vector.</param>
		/// <param name="first_seg">Index of first segment that currently holds information.</param>
		/// <param name="last_seg">Index of the last segment that currently holds informaton.</param>
		/// <param name="data">The data for the new row.</param>
		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		void insert(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr, segment_idx_t first_seg, segment_idx_t last_seg, Cs&&... data ) {
			resize(slot, vector_ptr, first_seg, last_seg); //if need be, grow the vector of segments
			allocate(slot, vector_ptr); //If need be, allocate a new segment and store it in the vector

			//copy or move the data to the new slot, using a recursive templated lambda
			auto f = [&]<size_t I, typename T, typename... Ts>(auto && fun, T && dat, Ts&&... dats) {
				if constexpr (vtll::is_atomic<T>::value) component_ptr<I>(slot)->store(dat);	//copy value for atomic
				else *component_ptr<I>(slot) = std::forward<T>(dat);							//move or copy
				if constexpr (sizeof...(dats) > 0) { fun.template operator() < I + 1 > (fun, std::forward<Ts>(dats)...); } //recurse
			};
			f.template operator() < 0 > (f, std::forward<Cs>(data)...);
		}

	protected:

		/// <summary>
		/// Return the segment index for a given slot.
		/// </summary>
		/// <param name="n">Slot.</param>
		/// <param name="vector_ptr">Pointer to the vector of segments, and offset</param>
		/// <returns>Index of the segment for the slot.</returns>
		static auto segment(table_index_t n, std::shared_ptr<seg_vector_t>& vector_ptr) {
			return segment_idx_t{ (n >> L) - vector_ptr->m_seg_offset };
		}

		/// <summary>
		/// If the vector of segments is too small, allocate a larger one and copy the previous segment pointers into it.
		/// Then make one CAS attempt. If the attempt succeeds, then remember the new segment vector.
		/// If the CAS fails because another thread beat us, then CAS will copy the new pointer so we can use it.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the segment vector.</param>
		/// <param name="first_seg">Index of first segment that currently holds information.</param>
		/// <param name="last_seg">Index of the last segment that currently holds informaton.</param>
		/// <returns></returns>
		inline auto resize(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr, segment_idx_t first_seg, segment_idx_t last_seg) {
			size_t num_seg = vector_ptr ? vector_ptr->m_segments.size() + vector_ptr->m_seg_offset : 0;	///< Current max number of segments
						
			while (slot >= N * num_seg) {		// Do we have enough entries in this table?			
				size_t new_size = SLOTS;		//default number of slots
				segment_idx_t offset{ 0 };		//start with zero offset
				if (vector_ptr) {				//we already have a segment vector?
					offset = vector_ptr->m_seg_offset;
					size_t num_segments = vector_ptr->m_segments.size();
					new_size = first_seg < (num_segments >> 1) ? num_segments * 2 : num_segments;
				}
				
				auto new_vector_ptr = std::make_shared<seg_vector_t>( //vector has always as many slots as its capacity is -> size==capacity
					seg_vector_t{ std::pmr::vector<std::atomic<segment_ptr_t>>{new_size, m_mr}, offset } //increase existing one
				);
				assert(new_vector_ptr);

				for (size_t i = 0; num_seg > 0 && i < last_seg - first_seg + 1; ++i) {
					auto ptr = vector_ptr->m_segments[first_seg + i].load();
					if (!ptr) {
						vector_ptr->m_segments[first_seg + i].wait(nullptr);
						ptr = vector_ptr->m_segments[first_seg + i].load();
					}
					new_vector_ptr->m_segments[i].store(ptr); ///< Copy segment pointers
					assert(new_vector_ptr->m_segments[i].load());
				}
				//new_vector_ptr->m_seg_offset += first_seg; //shift the used segments, account for spent segments

				if (m_seg_vector.compare_exchange_strong(vector_ptr, new_vector_ptr)) {	///< Try to exchange old segment vector with new
					vector_ptr = new_vector_ptr;										///< If success, remember for later
				} //Note: if we were beaten by other thread, then compare_exchange_strong itself puts the new value into vector_ptr
				num_seg = vector_ptr->m_segments.size() + vector_ptr->m_seg_offset;		///< Current max number of segments
			} //another thread could have beaten us here, so go back and retry
		}

		/// <summary>
		/// If there are not enough segments allocated, then allocate a new segment and try to CAS it.
		/// If this fails because we were beaten, then the newly allocated segment will be dealloacted atuomatically
		/// by the smart pointer.
		/// </summary>
		/// <param name="slot">Slot number in the table.</param>
		/// <param name="vector_ptr">Shared pointer to the segment vector.</param>
		/// <returns></returns>
		inline auto allocate(table_index_t slot, std::shared_ptr<seg_vector_t>& vector_ptr) {
			auto seg_idx = segment(slot, vector_ptr);						///< Index of segment we need
			auto seg_ptr = vector_ptr->m_segments[seg_idx].load();			///< Does the segment exist yet? If yes, increases use count.
			if (!seg_ptr) {													///< If not, create one
				auto new_seg_ptr = std::make_shared<segment_t>();			///< Create a new segment
				if (vector_ptr->m_segments[seg_idx].compare_exchange_strong(seg_ptr, new_seg_ptr)) {	///< Try to put it into seg vector, someone might beat us here
					vector_ptr->m_segments[seg_idx].notify_all();
				}
			}
			assert(vector_ptr->m_segments[seg_idx].load());
		}

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


	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltStack : public VlltTable<DATA, N0, ROW, SLOTS> {

	public:

		using VlltTable<DATA, N0, ROW, SLOTS>::N;
		using VlltTable<DATA, N0, ROW, SLOTS>::L;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_mr;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, SLOTS>::component_ptr;
		using VlltTable<DATA, N0, ROW, SLOTS>::insert;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::tuple_ref_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_ptr_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::seg_vector_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_idx_t;

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
			table_index_t m_next_free_slot{ 0 };	//index of next free slot
			table_index_t m_size{ 0 };				//number of valid entries
		};

		std::atomic<slot_size_t> m_size_cnt{ {table_index_t{0},table_index_t{0}} };	///< Next slot and size as atomic

		inline auto max_size() noexcept -> size_t;
	};

	/////
	// \brief Constructor of class VlltStack.
	// \param[in] r Max number of rows that can be stored in the table.
	// \param[in] mr Memory allocator.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline VlltStack<DATA, N0, ROW, SLOTS>::VlltStack(size_t r, std::pmr::memory_resource* mr) noexcept : VlltTable<DATA, N0, ROW, SLOTS>(r, mr) {};

	/////
	// \brief Destructor of class VlltStack.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline VlltStack<DATA, N0, ROW, SLOTS>::~VlltStack() noexcept { clear(); };

	/////
	// \brief Return number of rows when growing including new rows not yet established.
	// \returns number of rows when growing including new rows not yet established.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::max_size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::max(size.m_next_free_slot, size.m_size);
	};

	/////
	// \brief Return number of valid rows.
	// \returns number of valid rows.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::size() noexcept -> size_t {
		auto size = m_size_cnt.load();
		return std::min(size.m_next_free_slot, size.m_size);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<size_t I, typename C>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get(table_index_t n) noexcept -> C& {
		assert(n < size());
		return *component_ptr<I, C>(n);
	};

	/////
	// \brief Get a pointer to a particular component with index I.
	// \param[in] n Index to the entry.
	// \returns a pointer to the Ith component of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename C>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get(table_index_t n) noexcept -> C& requires vtll::unique<DATA>::value {
		return get<vtll::index_of<DATA, C>::value>(n);
	};

	/////
	// \brief Get a tuple with pointers to all components of an entry.
	// \param[in] n Index to the entry.
	// \returns a tuple with pointers to all components of entry n.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::get_tuple(table_index_t n) noexcept -> tuple_ref_t {
		return [&]<size_t... Is>(std::index_sequence<Is...>) { return std::tie(get<Is>(n)...); }(std::make_index_sequence<vtll::size<DATA>::value>{});
	};

	/////
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::push_back(Cs&&... data) noexcept -> table_index_t {
		//increase m_next_free_slot to announce your demand for a new slot -> slot is now reserved for you
		slot_size_t size = m_size_cnt.load();	///< Make sure that no other thread is popping currently
		while (size.m_next_free_slot < size.m_size || !m_size_cnt.compare_exchange_weak(size, slot_size_t{ table_index_t{ size.m_next_free_slot + 1 }, size.m_size })) {
			if (size.m_next_free_slot < size.m_size) { //here compare_exchange_weak was NOT called to copy manually
				size = m_size_cnt.load();
			}
		};

		//make sure there is enough space in the segment VECTOR - if not then change the old vector to a larger vector
		auto vector_ptr{ m_seg_vector.load() };	///< Shared pointer to current segment ptr vector, can be nullptr
		insert(size.m_next_free_slot, vector_ptr, segment_idx_t{ 0 }, segment_idx_t{ size.m_size>0 ? (size.m_size - 1) >> L : 0 }, std::forward<Cs>(data)...); ///< Make sure there are enough slots for segments

		slot_size_t new_size = m_size_cnt.load();	///< Increase size to validate the new row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_free_slot, table_index_t{ new_size.m_size + 1} }));

		return table_index_t{ size.m_next_free_slot };	///< Return index of new entry
	}

	/////
	// \brief Pop the last row if there is one.
	// \param[in] tup Pointer to tuple to move the row data into.
	// \param[in] del If true, then call desctructor on the removed slot.
	// \returns true if a row was popped.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::pop_back() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		slot_size_t size = m_size_cnt.load();
		if (size.m_next_free_slot == 0) return std::nullopt;	///< Is there a row to pop off?

		/// Make sure that no other thread is currently pushing a new row
		while (size.m_next_free_slot > size.m_size || 
			!m_size_cnt.compare_exchange_weak(size, slot_size_t{table_index_t{size.m_next_free_slot - 1}, size.m_size})) {

			if (size.m_next_free_slot > size.m_size) { size = m_size_cnt.load(); }
			if (size.m_next_free_slot == 0) return std::nullopt;	///< Is there a row to pop off?
		};

		auto idx = size.m_next_free_slot - 1;
		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(table_index_t{ idx })); }	//move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(table_index_t{ idx }); }				//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = component_ptr<i>(table_index_t{ idx })->load(); } 		//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(table_index_t{ idx })->~type(); }	///< Call destructor
			}
		);

		slot_size_t new_size = m_size_cnt.load();	///< Commit the popping of the row
		while (!m_size_cnt.compare_exchange_weak(new_size, slot_size_t{ new_size.m_next_free_slot, table_index_t{ new_size.m_size - 1} }));

		return ret; //RVO?
	}

	/////
	// \brief Pop all rows and call the destructors.
	// \returns number of popped rows.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::clear() noexcept -> size_t {
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
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::swap(table_index_t idst, table_index_t isrc) noexcept -> void {
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
	// Is lockless and threadsafe.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltStack<DATA, N0, ROW, SLOTS>::compress() noexcept -> void {
		auto vector_ptr{ m_seg_vector.load() };
		if (!vector_ptr) return;
		for (size_t i = vector_ptr->m_segments.size() - 1; i > (max_size() >> L); --i) {
			auto seg_ptr = vector_ptr->m_segments[i].load();
			if (seg_ptr) {
				if (seg_ptr.use_count() == 2) {
					std::shared_ptr<segment_t> new_seg_ptr;
					if (!vector_ptr->m_segments[i].compare_exchange_strong(seg_ptr, new_seg_ptr))	///< Try to put it into seg vector, someone might beat us here
						return;
				}
				else return;
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

	template<typename DATA, size_t N0 = 1 << 10, bool ROW = true, size_t SLOTS = 16>
	class VlltFIFOQueue : public VlltTable<DATA, N0, ROW, SLOTS> {

	public:
		using VlltTable<DATA, N0, ROW, SLOTS>::N;
		using VlltTable<DATA, N0, ROW, SLOTS>::L;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_seg_vector;
		using VlltTable<DATA, N0, ROW, SLOTS>::m_mr;
		using VlltTable<DATA, N0, ROW, SLOTS>::component_ptr;
		using VlltTable<DATA, N0, ROW, SLOTS>::insert;
		using VlltTable<DATA, N0, ROW, SLOTS>::segment;

		using tuple_opt_t = std::optional< vtll::to_tuple< vtll::remove_atomic<DATA> > >;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_ptr_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::seg_vector_t;
		using typename VlltTable<DATA, N0, ROW, SLOTS>::segment_idx_t;

		VlltFIFOQueue() {};

		template<typename... Cs>
			requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
		inline auto push_back(Cs&&... data) noexcept -> table_index_t;	///< Push new component data to the end of the table

		inline auto pop_front() noexcept -> tuple_opt_t;	///< Remove the last row, call destructor on components
		inline auto size() noexcept		 -> size_t;				///< Number of elements in the queue
		inline auto clear() noexcept	 -> size_t;			///< Set the number if rows to zero - effectively clear the table, call destructors
	
	protected:
		std::atomic<table_index_t>	m_next{ table_index_t{0} };	//next element to be taken out of the queue
		std::atomic<table_index_t>	m_consumed{};		//last element that was taken out and fully read and destroyed
		std::atomic<table_index_t>	m_next_free_slot{ table_index_t{0} };	//next element to write over
		std::atomic<table_index_t>	m_last{};			//last element that has been produced and fully constructed
	};


	///
	// \brief Push a new element to the end of the stack.
	// \param[in] data References to the components to be added.
	// \returns the index of the new entry.
	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	template<typename... Cs>
		requires std::is_same_v<vtll::tl<std::decay_t<Cs>...>, vtll::remove_atomic<DATA>>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::push_back(Cs&&... data) noexcept -> table_index_t {
		auto next_free_slot = m_next_free_slot.load();
		while (!m_next_free_slot.compare_exchange_weak(next_free_slot, table_index_t{ next_free_slot + 1 }));///< Slot number to put the new data into	

		auto vector_ptr{ m_seg_vector.load() };		///< Shared pointer to current segment ptr vector, can be nullptr
		auto first_segment = segment_idx_t{ 0 };
		auto last_segment = segment_idx_t{ 0 };

		if (vector_ptr && m_last.load().has_value()) {
			first_segment = segment(m_next.load(), vector_ptr);
			last_segment = segment(m_last.load(), vector_ptr);
		}

		insert(next_free_slot, vector_ptr, first_segment, last_segment, std::forward<Cs>(data)...);

		auto old_last = next_free_slot > 0 ? table_index_t{ next_free_slot - 1 } : table_index_t{};
		while (!m_last.compare_exchange_weak(old_last, next_free_slot));	///< Increase size to validate the new row

		return next_free_slot;	///< Return index of new entry
	}

	///
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::pop_front() noexcept -> tuple_opt_t {
		vtll::to_tuple<vtll::remove_atomic<DATA>> ret{};

		if (!m_last.load().has_value()) return std::nullopt;
		auto next = m_next.load();
		do {
			if (next > m_last.load()) return std::nullopt;
		} while (!m_next.compare_exchange_weak(next, table_index_t{ next + 1 }));  ///< Slot number to put the new data into	

		vtll::static_for<size_t, 0, vtll::size<DATA>::value >(	///< Loop over all components
			[&](auto i) {
				using type = vtll::Nth_type<DATA, i>;
				if		constexpr (std::is_move_assignable_v<type>) { std::get<i>(ret) = std::move(*component_ptr<i>(next)); } //move
				else if constexpr (std::is_copy_assignable_v<type>) { std::get<i>(ret) = *component_ptr<i>(next); } 			//copy
				else if constexpr (vtll::is_atomic<type>::value) { std::get<i>(ret) = component_ptr<i>(next)->load(); } 	//atomic

				if constexpr (std::is_destructible_v<type> && !std::is_trivially_destructible_v<type>) { component_ptr<i>(next)->~type(); }	///< Call destructor
			}
		);

		table_index_t consumed;
		do {
			consumed = (next > 0 ? table_index_t{ next - 1ull } : table_index_t{});
		} while (!m_consumed.compare_exchange_weak(consumed, next));

		return ret;		//RVO?
	}

	//
	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::size() noexcept -> size_t {
		return m_last.load() - m_consumed.load();
	}

	template<typename DATA, size_t N0, bool ROW, size_t SLOTS>
	inline auto VlltFIFOQueue<DATA, N0, ROW, SLOTS>::clear() noexcept -> size_t {
		size_t num = 0;
		while (pop_front().has_value()) { ++num; }
		return num;
	}


}
*/



