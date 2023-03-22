#pragma once

#include <limits>
#include <utility>

namespace vsty {

	template<typename T>
	concept Hashable = requires(T a) {
		{ std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
	};

	/**
	* \brief General strong type
	*
	* T...the type
	* P...phantom type as unique ID (can use __COUNTER__ or vsty::counter<>)
	*/
    template<typename T, auto P >
    struct strong_type_t {
        T value{};

        strong_type_t() noexcept = default;									//default constructible
        explicit strong_type_t(const T& v) noexcept { value = v; };			//explicit from type T
        explicit strong_type_t(T&& v) noexcept { value = std::move(v); };	//explicit from type T

        strong_type_t( strong_type_t<T, P> const &) noexcept = default;		//copy constructible
        strong_type_t( strong_type_t<T, P>&&) noexcept = default;			//move constructible

        strong_type_t<T, P>& operator=(T const& v) noexcept { value = v; return *this; };		//copy assignable from type T
        strong_type_t<T, P>& operator=(T&& v) noexcept { value = std::move(v); return *this; };	//copy assignable from type T

        strong_type_t<T, P>& operator=(strong_type_t<T, P> const&) noexcept = default;	//move assignable
        strong_type_t<T, P>& operator=(strong_type_t<T, P>&&) noexcept = default;			//move assignable

		operator const T& () const noexcept { return value; }	//retrieve value
		operator T& () noexcept { return value; }				//retrieve value

		auto operator<=>(const strong_type_t<T, P>& v) const = default;
	
		struct equal_to {
			constexpr bool operator()(const T& lhs, const T& rhs) const noexcept requires std::equality_comparable<std::decay_t<T>> { return lhs == rhs; };
		};
		
        struct hash {
            std::size_t operator()(const strong_type_t<T, P>& tag) const noexcept requires Hashable<std::decay_t<T>> { return std::hash<T>()(tag.value); };
        };
    };


	/**
	* \brief Strong type with a null value
	*
	* T...the type
	* D...default value (=null value)
	* P...phantom type as unique ID (can use __COUNTER__ or vsty::counter<>)
	*/	
	template<typename T, auto P, auto D>
	struct strong_type_null_t : strong_type_t<T, P> {
		using strong_type_t<T,P>::value;
		static const T null{D};
		strong_type_null_t() { value = D; };
		explicit strong_type_null_t(const T& v) : strong_type_t<T,P>(v) {};
		bool has_value() const noexcept { return value != D; }
	};


	/**
	* \brief Strong integral type, like size_t or uint32_t. Can be split into three integral values 
	* (upper, middle and lower).
	* This works ONLY if the integral type is unsigned!!
	*
	* T...the integer type
	* P...phantom type as unique ID (can use __COUNTER__ or vsty::counter<>)
	* U...number of upper bits (if integer is cut into 2 values), or else 0
	* M...number of middle bits (if integer is cut into 3 values), or else 0
	*/
	template<typename T, auto P, size_t U = 0, size_t M = 0>
		requires std::is_integral_v<std::decay_t<T>>
	struct strong_integral_t : strong_type_t<T, P>  {

		static const size_t BITS = sizeof(T) * 8ull;
		static_assert(BITS >= U+M);

		static const size_t L = BITS - U - M; //number of lower bits (if integer is cut into 2/3 values)

		static consteval T lmask() {
			if constexpr (L == 0) return static_cast<T>(0ull);
			else if constexpr (U == 0 && M == 0) return static_cast<T>(~0ull);
			else return static_cast<T>(~0ull) >> (BITS - L);
		}

		static consteval T umask() {
			if constexpr (U == 0) return static_cast<T>(0ull);
			else if constexpr (M == 0 && L == 0) return static_cast<T>(~0ull);
			else return static_cast<T>(~0ull) << (BITS - U);
		}

		static const T LMASK = lmask();
		static const T UMASK = umask();
		static const T MMASK = ~(LMASK | UMASK);

		using strong_type_t<T, P>::value;

		strong_integral_t() noexcept = default;											//default constructible
		explicit strong_integral_t(const T& v) noexcept : strong_type_t<T, P>(v) {};	//explicit from type T
		explicit strong_integral_t(T&& v) noexcept : strong_type_t<T, P>(v) {};			//explicit from type T

		strong_integral_t(strong_integral_t<T, P, U, M> const&) noexcept = default;	//copy constructible
		strong_integral_t(strong_integral_t<T, P, U, M>&& v) noexcept = default;	//move constructible

		strong_integral_t<T, P, U, M>& operator=(T const& v) noexcept { value = v; return *this; };		//copy assignable
		strong_integral_t<T, P, U, M>& operator=(T&& v) noexcept { value = std::move(v); return *this; };	//copy assignable

		strong_integral_t<T, P, U, M>& operator=(strong_integral_t<T, P, U, M> const&) noexcept = default;		//move assignable
		strong_integral_t<T, P, U, M>& operator=(strong_integral_t<T, P, U, M>&&) noexcept = default;	//move assignable

		operator const T& () const noexcept { return value; }	//retrieve value
		operator T& () noexcept { return value; }				//retrieve value

		//-----------------------------------------------------------------------------------

		auto operator<=>(const strong_integral_t<T, P, U, M>& v) const = default;

		T operator<<(const size_t N) noexcept { return value << N; };
		T operator>>(const size_t N) noexcept { return value >> N; };
		T operator&(const size_t N) noexcept { return value & N; };

		auto operator++() noexcept { ++value; return *this; };
		auto operator++(int) noexcept { return strong_integral_t<T, P, U, M>(value++); };
		auto operator--() noexcept { --value; return *this; };
		auto operator--(int) noexcept { return strong_integral_t<T, P, U, M>(value--); };

		auto set_upper(T v) noexcept requires std::is_unsigned_v<std::decay_t<T>> { if constexpr (U > 0) { value = (value & (LMASK | MMASK)) | ( (v << (L+M)) & UMASK); } } 
		auto get_upper()    noexcept requires std::is_unsigned_v<std::decay_t<T>> { if constexpr (U > 0) { return value >> (L+M); } return static_cast<T>(0); }	
		auto set_middle(T v) noexcept requires std::is_unsigned_v<std::decay_t<T>> { if constexpr (M > 0) { value = (value & (LMASK | UMASK)) | ((v << L) & MMASK); } }
		auto get_middle()    noexcept requires std::is_unsigned_v<std::decay_t<T>> { if constexpr (M > 0) { return (value & MMASK) >> L; } return static_cast<T>(0); }
		auto set_lower(T v) noexcept requires std::is_unsigned_v<std::decay_t<T>> { value = (value & (UMASK | MMASK)) | (v & LMASK); }
		auto get_lower()    noexcept requires std::is_unsigned_v<std::decay_t<T>> { return value & LMASK; }
	};


	/**
	* \brief Strong integral type with a null value. 
	*
	* T...the type
	* D...default value (=null value)
	* P...phantom type as unique ID (can use __COUNTER__ or vsty::counter<>)
	* U...number of upper bits (if integer is cut into 2 values), or else 0
	* M...number of middle bits (if integer is cut into 3 values), or else 0
	*/
	template<typename T, auto P, auto D = std::numeric_limits<T>::max(), size_t U = 0, size_t M = 0>
		requires std::is_integral_v<std::decay_t<T>>
	struct strong_integral_null_t : strong_integral_t<T, P, U, M> {
		using strong_integral_t<T, P, U, M>::value;
		static const T null{ D };
		strong_integral_null_t() { value = D; };
		explicit strong_integral_null_t(const T& v) : strong_integral_t<T, P, U, M>(v) {};
		bool has_value() const noexcept { return value != D; }
	};


	//--------------------------------------------------------------------------------------------
	//type counter lifted from https://mc-deltat.github.io/articles/stateful-metaprogramming-cpp20

	template<size_t N>
	struct reader { friend auto counted_flag(reader<N>); };

	template<size_t N>
	struct setter {
		friend auto counted_flag(reader<N>) {}
		static constexpr size_t n = N;
	};

	template< auto Tag, size_t NextVal = 0 >
	[[nodiscard]] consteval auto counter_impl() {
		constexpr bool counted_past_value = requires(reader<NextVal> r) { counted_flag(r); };

		if constexpr (counted_past_value) {
			return counter_impl<Tag, NextVal + 1>();
		}
		else {
			setter<NextVal> s;
			return s.n;
		}
	}

	template< auto Tag = [] {}, auto Val = counter_impl<Tag>() >
	constexpr auto counter = Val;

}


