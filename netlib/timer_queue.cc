#include <netlib/timer_queue.h>

#include <assert.h>
#include <string.h> // strerror()
#include <sys/timerfd.h> // timerfd_*
#include <unistd.h> // close(), read()

#include <algorithm>

#include <netlib/channel.h>
#include <netlib/event_loop.h>
#include <netlib/logging.h>
#include <netlib/timer.h>
#include <netlib/timer_id.h>

using std::bind;
using std::copy;
using std::make_pair;
using std::pair;
using std::unique_ptr;
using std::vector;
using netlib::Channel;
using netlib::EventLoop;
using netlib::TimerId;
using netlib::TimerQueue;

TimerQueue::TimerQueue(EventLoop *owner_loop):
	owner_loop_(owner_loop),
	timer_fd_(CreateTimerFd()), // Create a new timer.
	timer_fd_channel_(owner_loop, timer_fd_),
	expired_timer_(), // set<> container.
	active_timer_set_by_expiration_()
{
	timer_fd_channel_.set_requested_event_read(); // Monitor IO read event.
	timer_fd_channel_.set_read_callback(bind(&TimerQueue::HandleRead, this));
}

TimerQueue::~TimerQueue()
{
	::close(timer_fd_);
	// TODO: Do not remove channel, since we're in EventLoop::dtor();
	// TODO: If we don't use shared_ptr(since it cost too much) or unique_ptr
	// (since the const property of set element), we should use what to avoid
	// `delete Timer*;` by ourself?
	for(ExpirationTimerPairSet::iterator it = active_timer_set_by_expiration_.begin();
	        it != active_timer_set_by_expiration_.end(); ++it)
	{
		delete it->second;
		// it->second = nullptr; error: assignment of member ‘pair<TimeStamp, Timer*>
		// ::second’ in read-only object
	}
}

// Construct a new timer and Call RunInLoop to add timer to set in the lop thread.
// Return a TimerId object that encapsulates this timer.
TimerId TimerQueue::AddTimer(const TimerCallback &callback,
                             TimeStamp expired_time,
                             double interval)
{
	// 1. Create a Timer object based on arguments.
	Timer *timer = new Timer(callback, expired_time, interval);
	// 2. Add this new timer in loop thread by calling RunInLoop().
	owner_loop_->RunInLoop(bind(&TimerQueue::AddTimerInLoop, this, timer));
	// 3. Return the inserted timer as a TimerId object.
	return TimerId(timer);
}

// Add timer in the loop thread. Always as a functor passed to RunInLoop().
void TimerQueue::AddTimerInLoop(Timer *timer)
{
	owner_loop_->AssertInLoopThread();
	// 1. Insert this timer into timer set. Return true if this timer will expire first.
	bool is_first_expired = InsertIntoTimerPairSet(timer);
	// 2. If this timer will expire first, update timer_fd_'s expiration time.
	if(is_first_expired)
	{
		SetExpirationTime(timer->expiration());
	}
}

// Create a new timer fd. Called by TimerQueue::TimerQueue(EventLoop *loop).
int TimerQueue::CreateTimerFd()
{
	int timer_fd = ::timerfd_create(CLOCK_MONOTONIC,
	                                TFD_NONBLOCK | TFD_CLOEXEC);
	if(timer_fd < 0)
	{
		LOG_FATAL("CreateTimerFd() failed.");
	}
	return timer_fd;
}

// Get the expired timers relative to `now` and store them in expired_time_ vector.
void TimerQueue::GetExpiredTimer(TimeStamp now)
{
	// 1. Clear the old expired timer vector.
	expired_timer_.clear();

	// 2. Set sentry to search the set and get the first not expired timer iterator.
	// sentry is the biggest timer-pair whose time-stamp value is `now`.
	ExpirationTimerPair sentry = make_pair(now, reinterpret_cast<Timer*>(UINTPTR_MAX));
	ExpirationTimerPairSet::iterator first_not_expired = active_timer_set_by_expiration_.lower_bound(sentry);
	//(1)	Not find the iterator that we can insert sentry into timer set, that is, all timers
	//		in timer set are already expired.
	//(2)	If can find, the first not expired timer's expiration time must be greater than
	//		`now` since sentry is the biggest timer whose expiration time is `now`.
	assert(first_not_expired == active_timer_set_by_expiration_.end() || now < first_not_expired->first);

	// 3. Copy all the expired timer's pointer from active_timer_set_by_expiration_ to expired_timer_.
	for(ExpirationTimerPairSet::iterator it = active_timer_set_by_expiration_.begin(); it != first_not_expired; ++it)
	{
		expired_timer_.push_back(it->second); // Pass by value: copy a pointer.
	}

	// 4. Erase the expired timers in the active_timer_set_by_expiration_, this don't destruct the timer.
	// TODO: what happens when set.erase(iterator)?
	active_timer_set_by_expiration_.erase(active_timer_set_by_expiration_.begin(), first_not_expired);
}

// Insert the specified timer into timer set. Return true if this timer will expire first.
bool TimerQueue::InsertIntoTimerPairSet(Timer *timer)
{
	bool is_first_expired = false;
	TimeStamp expired_time = timer->expiration();
	ExpirationTimerPairSet::iterator it = active_timer_set_by_expiration_.begin();
	// When one of the following conditions satisfies, this timer expires first:
	// 1. Timer set has no timer.
	// 2. This timer's expiration time is less than the smallest expiration time in timer set.
	if(it == active_timer_set_by_expiration_.end() || expired_time < it->first)
	{
		is_first_expired = true;
	}
	// Make a new pair for this timer and insert it to the timer set.
	pair<ExpirationTimerPairSet::iterator, bool> insert_result =
	    active_timer_set_by_expiration_.insert(make_pair(expired_time, timer));
	assert(insert_result.second == true);
	return is_first_expired;
}

// The callback for IO read event, in this case, the timer fd alarms.
void TimerQueue::HandleRead()
{
	owner_loop_->AssertInLoopThread();
	// 1. Get now time and Invoke `ReadTimerFd()` to read form timer_fd_.
	TimeStamp now(TimeStamp::Now());
	ReadTimerFd(now);
	// 2.	Get the expired timers relative to `now` and store them in expired_time_ vector.
	//		Run each timer's callback_.
	GetExpiredTimer(now);
	for(TimerVector::iterator it = expired_timer_.begin(); it != expired_timer_.end(); ++it)
	{
		(*it)->Run(); // Timer_Pointer->Run() runs timer object's callback_.
	}
	// 3. Refresh state for the next expiration.
	Refresh(now);
}

// Call ::read to read from `timer_fd` at `time_stamp` time.
void TimerQueue::ReadTimerFd(TimeStamp time_stamp)
{
	// If the timer has expired one or more times since its settings were last modified using
	// timerfd_settime(), or since the last successful read(2), then the buffer given to read(2)
	// returns an unsigned 8-byte integer(uint64_t) containing the number of expirations
	// that have occurred. The returned value is in host byte order.
	uint64_t expiration_number = 0;
	int readn = static_cast<int>(::read(timer_fd_, &expiration_number, 8));
//	LOG_INFO("expiration number = %d, time = %s",
//	         static_cast<int>(expiration_number), time_stamp.ToString());
	if(readn != 8)
	{
		LOG_INFO("TimerQueue::ReadCallback read %d bytes instead of 8.", readn);
	}
}

// Restart or delete expired timer and update timer_fd_'s expiration time.
void TimerQueue::Refresh(TimeStamp now)
{
	// 1. For expired timer:
	//		(1). Restart if it can repeat and not be canceled.
	//		(2). Delete it otherwise.
	for(TimerVector::iterator it = expired_timer_.begin(); it != expired_timer_.end(); ++it)
	{
		TimerSequencePair timer(*it, (*it)->sequence());
		// Can repeat: restart timer and insert the updated Timer* into timer pair set again.
		if((*it)->repeat() &&
		        canceling_timer_set_.find(timer) == canceling_timer_set_.end())
		{
			(*it)->Restart(now);
			InsertIntoTimerPairSet(*it);
		}
		else
		{
			delete (*it); // This timer can't repeat, delete it explicitly.
			(*it) = nullptr;
			// Since we always `expired_timer_.clear()` before GetExpiredTimer(),
			// we don't need erase this deleted Timer* in expired_time_ vector.
		}
	}
	// 2. Set next expiration time.
	TimeStamp next_expiration;
	if(active_timer_set_by_expiration_.empty() == false)
	{
		// set<> is ordered by itself.
		next_expiration = (*(active_timer_set_by_expiration_.begin()->second)).expiration();
	}
	if(next_expiration.IsValid() == true)
	{
		LOG_INFO("SET-Refresh");
		SetExpirationTime(next_expiration);
	}
}

// Set the expiration time to `expiration - Now()`. The argument `expiration` is the
// absolute expiration time, that is, there will have timers that expire at `expiration`
// seconds since Epoch.
void TimerQueue::SetExpirationTime(TimeStamp expiration)
{
	// 1. Convert `expiration` to a `struct timespec`
	int64_t microsecond = expiration.microsecond_since_epoch()
	                      - TimeStamp::Now().microsecond_since_epoch();
	// Suppose:
	//					RunEvery(callback, 1);	// Timer 1
	//					RunEvery(callback, 2);	// Timer 2
	//					RunAfter(callback, 2);	// Timer 3
	// At 1 second: timer 1 expires -> poll return -> callback() -> timer 1 restart to expire
	// at 2 second. Roughly speaking, timer 1, 2 and 3 will all expire at 2 second. But since
	// timer 1 runs callback and then set expiration time to 1 second later, its expiration
	// time is a little later than timer 2 and 3's expiration time, about tens of microseconds,
	// which is the following case. When we call `Refresh()->SetExpirationTime();`, the
	// timer 1 may have already expired(i.e., microsecond < 0) or about to expire
	// (i.e., 0 <= microsecond < 100). For both cases, we set the expiration time to 100
	// microsecond to let the timer expire almost instantly, thus poll will return and runs
	// this timer's callback instantly.
	if(microsecond < 100)
	{
		LOG_INFO("actual length = %lld\n", static_cast<long long int>(microsecond))
		microsecond = 100;
	}
	struct timespec ts;
	ts.tv_sec = static_cast<time_t>(microsecond / TimeStamp::kMicrosecondPerSecond);
	ts.tv_nsec =
	    static_cast<long>((microsecond % TimeStamp::kMicrosecondPerSecond) * 1000);

	// 2. Call timerfd_settime to set the new expiration time for the timer set.
	struct itimerspec new_value;
	bzero(&new_value, sizeof(new_value));
	new_value.it_value = ts;
	// int timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
	//                     struct itimerspec *old_value);
	// flags = 0: start a relative timer. new_value.it_value specifies a time relative to
	// the current value of the clock specified by clockid.
	int ret = ::timerfd_settime(timer_fd_, 0, &new_value, NULL);
	// Return 0 on success; -1 on error and set errno to indicate the error.
	if(ret)
	{
		LOG_FATAL("timerfd_settime error: %s", strerror(errno));
	}
}

void TimerQueue::Cancel(TimerId timer_id)
{
	loop_->RunInLoop(bind(&TimerQueue::CancelInLoop, this, timer_id));
}

void TimerQueue::CancelInLoop(TimerId timer_id)
{
	loop_->AssertInLoopThread();
	assert(active_timer_set_by_expiration_.size() == active_timer_set_by_address_.size());

	TimerSequencePair timer(timer_id.timer_, timer_id.sequence_);
	TimerSequencePairSet::iterator it = active_timer_set_by_address_.find(timer);
	if(it != active_timer_set_by_address_.end())
	{
		assert(active_timer_set_by_expiration_.erase(
		           ExpirationTimerPair(it->first->expiration(), it->first)) == 1);
		delete it->first;
		active_timer_set_by_address_.erase(it);
	}
	else if(calling_expired_timer_) // TODO: what's meaning?
	{
		canceling_timer_set_.insert(timer);
	}
	assert(active_timer_set_by_expiration_.size() == active_timer_set_by_address_.size());
}
