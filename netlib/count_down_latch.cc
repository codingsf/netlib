#include <netlib/count_down_latch.h>

using netlib::CountDownLatch;
using netlib::MutexLockGuard;

CountDownLatch::CountDownLatch(int number):
	mutex_(),
	condition_(mutex_),
	count_(number)
{}

int CountDownLatch::count() const
{
	MutexLockGuard lock(mutex_);
	return count_;
}

void CountDownLatch::CountDown()
{
	MutexLockGuard lock(mutex_);
	--count_;
	if(count_ == 0)
	{
		// Broadcast indicates state change rather than resource availability.
		condition_.NotifyAll();
	}
}

void CountDownLatch::Wait()
{
	// Must first get lock and then Wait() on condition.
	MutexLockGuard lock(mutex_);
	while(count_ > 0)
	{
		condition_.Wait();
	}
}