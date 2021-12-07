#ifndef INTTYPE_H
#define INTTYPE_H


/**
* \brief Strong type for integers.
* 
* T...the integer type
* P...phantom type as unique ID
* D...default value (=null value)
* U...number of upper bits (if integer is cut into 2 values)
*/
template<typename T, typename P, auto D = -1, size_t U = 0>
struct int_type {
private:
	static const size_t L = sizeof(T) * 8 - U; //number of lower bits (if integer is cut into 2 values)
	const size_t LMASK = (1ULL << L) - 1ULL;
	const size_t UMASK = ((1ULL << U) - 1ULL) << L;

	static const T null = static_cast<T>(D); //null value

	T m_value{null};

public:

	int_type() = default;

	/**
	* \brief Constructor.
	* \param[in] u A POD type that is used for setting the value.
	*/
	template<typename V>
	requires (std::is_convertible_v<std::decay_t<V>, T> && std::is_pod_v<std::decay_t<V>>)
	explicit int_type(V&& u) noexcept : m_value{ static_cast<T>(u) } {};

	/**
	* \brief Copy assignment.
	* \param[in] rhs Any POD int type.
	*/
	template<typename V>
	requires (std::is_convertible_v<std::decay_t<V>, T> && std::is_pod_v<std::decay_t<V>>)
	void operator=(V&& rhs) noexcept { m_value = static_cast<T>(rhs); };

	/**
	* \brief Yield the int value.
	* \returns the int value.
	*/
	operator const T& () const { 
		return m_value;
	}

	/**
	* \brief Yield the int value.
	* \returns the int value.
	*/
	operator T& () { 
		return m_value;
	}

	/**
	* \brief Comparison operator.
	* \returns the default comparison.
	*/
	auto operator<=>(const int_type<T, P, D, U>& v) noexcept { return operator () <=> v.operator (); };

	/**
	* \brief Comparison operator.
	* \returns the comparison between this value and another int.
	*/
	template<typename V>
	requires std::is_convertible_v<V, T>
	auto operator<(const V& v) noexcept { return m_value < static_cast<T>(v); };

	/**
	* \brief Left shift operator.
	* \param[in] L Number of bits to left shift.
	* \returns the value left shifted.
	*/
	T operator<<(const size_t L) noexcept { return m_value << L; };

	/**
	* \brief Right shift operator.
	* \param[in] L Number of bits to right shift.
	* \returns the value left shifted.
	*/
	T operator>>(const size_t L) noexcept { return m_value >> L; };

	/**
	* \brief And operator.
	* \param[in] L Number that should be anded bitwise.
	* \returns the bew value that was anded to the old value.
	*/
	T operator&(const size_t L) noexcept { return m_value & L; };

	/**
	* \brief Pre-increment operator.
	* \returns the value increased by 1.
	*/
	int_type<T, P, D, U> operator++() noexcept {
		m_value++;
		if( !has_value() ) m_value = 0;
		return *this;
	};

	/**
	* \brief Post-increment operator.
	* \returns the old value before increasing by 1.
	*/
	int_type<T, P, D, U> operator++(int) noexcept {
		int_type<T, P, D, U> res = *this;
		m_value++;
		if (!has_value()) m_value = 0;
		return res;
	};

	/**
	* \brief Pre-decrement operator.
	* \returns the value decreased by 1.
	*/
	int_type<T, P, D, U> operator--() noexcept {
		--m_value;
		if (!has_value()) --m_value;
		return *this;
	};

	/**
	* \brief Post-decrement operator.
	* \returns the value before decreasing by 1.
	*/
	int_type<T, P, D, U> operator--(int) noexcept {
		int_type<T, P, D, U> res = *this;
		m_value--;
		if (!has_value()) m_value--;
		return res;
	};

	/**
	* \brief Create a hash value.
	*/
	struct hash {
		/**
		* \param[in] tg The input int value.
		* \returns the hash of the int value.
		*/
		std::size_t operator()(const int_type<T, P, D, U>& tg) const { return std::hash<T>()(tg.m_value); };
	};

	/**
	* \brief Equality comparison as function.
	* \returns true if the value is not null (the default value).
	*/
	struct equal_to {
		constexpr bool operator()(const T& lhs, const T& rhs) const { return lhs == rhs; };
	};

	/**
	* \brief Determine whether the value is not null.
	* \returns true if the value is not null (the default value).
	*/
	bool has_value() const {
		return m_value != null;
	}

	/**
	* \brief Set the upper value (if split into two integers).
	* \param[in] v New upper value.
	*/
	void set_upper(T v) {
		m_value = (m_value & LMASK) | (v << L);
	}

	/**
	* \brief Return the upper value (if split into two integers).
	* \returns the upper value.
	*/
	T get_upper() {
		return (m_value >> L);
	}

	/**
	* \brief Set the lower value (if split into two integers).
	* \param[in] v New lower value.
	*/
	void set_lower(T v) {
		m_value = (m_value & UMASK) | (v & LMASK);
	}

	/**
	* \brief Return the lower value (if split into two integers).
	* \returns the lower value.
	*/
	T get_lower() {
		return (m_value & LMASK);
	}

};


#endif
