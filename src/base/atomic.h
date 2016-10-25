#ifndef NETLIB_SRC_BASE_ATOMIC_H_
#define NETLIB_SRC_BASE_ATOMIC_H_

#include <stdint.h> // int32_t, int64_t

#include <base/non_copyable.h> // netlib::NonCopyable class.

namespace netlib
{
namespace detail
{
template<typename T>
class AtomicInteger: public netlib::NonCopyable // TODO: What use of this class???
{
public:
	AtomicInteger(): value_(0) {}

	T Get() // Get the value of `value_` atomically.
	{
		// Implements an atomic load operation and returns the contents of *ptr.
		return __atomic_load_n(&value_, __ATOMIC_SEQ_CST);
		// When implementing patterns for these built-in functions, the memory order
		// parameter can be ignored as long as the pattern implements the most restrictive
		// __ATOMIC_SEQ_CST memory order. Any of the other memory orders execute
		// correctly with this memory order but they may not execute as efficiently as
		// they could with a more appropriate implementation of the relaxed requirements.
	}

	// value_ += value; and return the value that had previously been in value_.
	T GetAndAdd(T value)
	{
		// Perform the add operation and return the value that had previously been in *ptr.
		return __atomic_fetch_add(&value_, value, __ATOMIC_SEQ_CST);
	}

	T AddAndGet(T value) // Get the value of `value_` after addition.
	{
		// TODO: Why don't use __atomic_add_fetch?
		return GetAndAdd(value) + value;
	}

	T IncrementAndGet() // Get the value of `value_ + 1`
	{
		return AddAndGet(1);
	}

	T DecrementAndGet() // Get the value of `value_ - 1`
	{
		return AddAndGet(-1);
	}

	void Add(T value) // value_ + value
	{
		GetAndAdd(value);
	}

	void Increment() // value_ + 1
	{
		IncrementAndGet();
	}

	void Decrement() // value_ - 1
	{
		DecrementAndGet();
	}

	T GetAndSet(T new_value)
	{
		// Implement an atomic exchange operation: write val into *ptr,
		// and returns the previous contents of *ptr.
		return __atomic_exchange_n(&value_, new_value, __ATOMIC_SEQ_CST);
	}

private:
	volatile T value_; // TODO: when to use `volatile`
};
}

using AtomicInt32 = detail::AtomicInteger<int32_t>;
using AtomicInt64 = detail::AtomicInteger<int64_t>;
}
#endif // NETLIB_SRC_BASE_ATOMIC_H_
