#ifndef NETLIB_NETLIB_TIMER_QUEUE_H_
#define NETLIB_NETLIB_TIMER_QUEUE_H_

#include <set>
#include <vector>

#include <netlib/callback.h>
#include <netlib/channel.h>
#include <netlib/non_copyable.h>
#include <netlib/time_stamp.h>

namespace netlib
{

class EventLoop;
class Timer;
class TimerId;

// FIXME: There is a BUG!!!

// Interface:
// Ctor -> -CreateTimerFd -> -HandleRead.
//		-HandleRead -> -ReadTimerFd -> -GetAndRemoveExpiredTimer -> -Refresh.
//			-Refresh -> -InsertIntoActiveTimerSet -> -SetExpiredTime.
// Dtor.
// AddTimer -> -AddTimerInLoop -> -InsertIntoActiveTimerSet -> SetExpiredTime.
// CancelTimer -> -CancelTimerInLoop.

// A best effort timer queue. No guarantee that the callback will be on time.
class TimerQueue: public NonCopyable
{
public:
	TimerQueue(EventLoop *owner_loop);
	~TimerQueue();

	// Schedule the callback to be run at given time, repeats if `interval > 0.0`.
	// Thread safe: always add timer in the loop thread by calling
	// RunInLoop(AddTimerInLoop).
	// Used by EventLoop, and EventLoop encapsulates it to be RunAt(), RunAfter()...
	// Construct a new timer based on the arguments; Insert it to timer set;
	// Return a TimerId object that encapsulates this timer.
	TimerId AddTimer(const TimerCallback &callback,
	                 TimeStamp expired_time,
	                 double interval);
	void CancelTimer(TimerId timer_id);

private:
	using TimerVector = std::vector<Timer*>;
	using ExpirationTimerPair = std::pair<TimeStamp, Timer*>;
	using ExpirationTimerPairSet = std::set<ExpirationTimerPair>;

	// Create a new timer fd. Called by TimerQueue::TimerQueue(EventLoop *loop).
	int CreateTimerFd();
	// The callback for IO read event, in this case, the timer fd alarms.
	void HandleRead();
	// Call ::read to read from `timer_fd` at `time_stamp` time.
	void ReadTimerFd(TimeStamp time_stamp);
	// Get the expired timers relative to `now` and store them in expired_timer_vector_ vector.
	void GetAndRemoveExpiredTimer(TimeStamp now);
	// Restart or delete expired timer and update timer_fd_'s expiration time.
	void Refresh(TimeStamp now);
	// Insert the specified timer into timer set. Return true if this timer will expire first.
	bool InsertIntoActiveTimerSet(Timer *timer);
	// Set timer_fd_'s expiration time to be `expiration` argument.
	void SetExpiredTime(TimeStamp expiration);
	// Add timer in the loop thread. Always as a functor passed to RunInLoop().
	void AddTimerInLoop(Timer *timer);
	void CancelTimerInLoop(TimerId timer_id);

	EventLoop *owner_loop_;
	const int timer_fd_; // The timer file descriptor of this Timer object.
	Channel timer_fd_channel_; // Monitor the IO(readable) events on timer_fd_.
	// Store the expired Timer object's pointer.
	TimerVector expired_timer_vector_;
	// Active Timer set sorted by <timer_expired_time, timer_object_address>:
	// first sorted by the expired time; if two or more timers have the same
	// expired time, distinguish them by their object's address.
	ExpirationTimerPairSet active_timer_set_;
	// For CancelTimer():
	std::set<int64_t> canceling_timer_sequence_set_;
};

// Don't use unique_ptr<Timer>:
// 1.	Both iterator and const_iterator types of set give us read-only access to
//		the elements in the set. Thus, for timers that can still restart, we can't update
//		their time-stamp value in set, we must delete them in the set(only erase the
//		pointer, not delete the object), and insert the updated timer into set again.
// 2.	We can't erase/insert elements in the loop that traverses the set, because this will
//		make iterators invalid. We must first get a copy of all timers that have expired,
//		erase them in the set, update the copy Timer*, and insert the updated Timer*.
// 3.	unique_ptr's copy constructor is deleted, we can't copy a unique_ptr.
// 4.	We can't use `u.release()`: Relinquish control of the pointer u had held;
//		return the pointer u has held and make u null.
//			Try 1:	`Timer *timer = (it->second).release();`
//			Error:	passing ‘const unique_ptr<Timer>’ as ‘this’ argument of
//						‘unique_ptr<_Tp, _Dp>::pointer unique_ptr<_Tp, _Dp>::release()
//						discards qualifiers [-fpermissive]
//			This error means we should pass a non-const unique_ptr to release().
//			Try 2:	`Timer*timer=(static_cast<unique_ptr<Timer>>(it->second)).release();`
//			Error:	use of deleted function ‘unique_ptr(const unique_ptr<_Tp,_Dp>&)
//						/usr/include/c++/4.8/bits/unique_ptr.h:273:7: error: declared here
//							unique_ptr(const unique_ptr&) = delete;
//			So, we can't get the non-const unique_ptr from const version unique_ptr in set.
// 5.	The Dirty method is using get() to get the raw pointer of this unique_ptr<Timer>,
//		create a new Timer object that is the updated object of this Timer. But this leads to
//		too many timer objects and waste a lot of memory and lower performance.

}

#endif // NETLIB_NETLIB_TIMER_QUEUE_H_
