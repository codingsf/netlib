#include <netlib/event_loop.h>

#include <assert.h> // assert()
#include <sys/eventfd.h> // eventfd()
#include <unistd.h> // read(), close(), write()
#include <stdint.h> // uint64_t
#include <signal.h> // signal()

#include <algorithm> // find()

#include <netlib/channel.h>
#include <netlib/logging.h>
#include <netlib/epoller.h>
#include <netlib/thread.h>
#include <netlib/timer_queue.h>

using std::bind;
using netlib::EventLoop;
using netlib::Thread;
using netlib::TimerId;
using netlib::TimerCallback;
using netlib::TimeStamp;

// Signal `SIGPIPE`: write to pipe with no readers. Default action: terminate.
class IgnoreSigPipe
{
public:
	IgnoreSigPipe()
	{
		::signal(SIGPIPE, SIG_IGN);
	}
};
IgnoreSigPipe ignore_sig_pipe_object;

// Every thread has its own instance of __thread variable.
__thread EventLoop *t_loop_in_this_thread = nullptr;

EventLoop::EventLoop():
	looping_(false),
	quit_(false),
	thread_id_(Thread::ThreadId()),
	epoller_(new Epoller(this)),
	epoll_return_time_(),
	mutex_(),
	calling_pending_functor_(false),
	event_fd_(CreateEventFd()),
	event_fd_channel_(new Channel(this, event_fd_)),
	timer_queue_(new TimerQueue(this))
{
	LOG_DEBUG("EventLoop created %p in thread %d", this, thread_id_);
	// One loop per thread means that every thread can have only one EventLoop object.
	// If this thread already has another EventLoop object, abort this thread.
	if(t_loop_in_this_thread != nullptr)
	{
		LOG_FATAL("Another EventLoop %p exists in this thread %d",
		          t_loop_in_this_thread, thread_id_);
	}
	else
	{
		// The thread that creates EventLoop object is the loop thread,
		// whose main function is running EventLoop::Loop().
		t_loop_in_this_thread = this;
	}
	event_fd_channel_->set_event_callback(Channel::READ_CALLBACK,
	                                       bind(&EventLoop::HandleRead, this));
	event_fd_channel_->set_requested_event(Channel::READ_EVENT);
}
// int eventfd(unsigned int initval, int flags);
// eventfd() creates an "eventfd object" that can be used as an event wait/notify
// mechanism by user-space applications, and by the kernel to notify user-space
// applications of events. The object contains an unsigned 64-bit integer(uint64_t)
// counter that is maintained by the kernel. This counter is initialized with `initval`.
// EFD_CLOEXEC		Set close-on-exec(FD_CLOEXEC) flag on the returned fd.
// EFD_NONBLOCK	Set the O_NONBLOCK file status flag.
// Return a new file descriptor on success; -1 on error and errno is set.
int EventLoop::CreateEventFd()
{
	int wakeup_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if(wakeup_fd < 0)
	{
		LOG_FATAL("eventfd(): FATAL");
	}
	return wakeup_fd;
}
// The value returned by read(2) is in host byte order.
// 1.	If EFD_SEMAPHORE was not specified and the eventfd counter has a nonzero
//		value, then a read(2) returns 8 bytes containing that value, and the counter's
//		value is reset to zero.
// 2.	If the eventfd counter is zero, the call fails with the error EAGAIN if the file
//		descriptor has been made nonblocking.
void EventLoop::HandleRead()
{
	uint64_t value;
	int read_byte = static_cast<int>(::read(event_fd_, &value, sizeof value));
	if(read_byte != sizeof value)
	{
		LOG_ERROR("EventLoop::HandleRead() reads %d bytes instead of 8", read_byte);
	}
}

EventLoop::~EventLoop()
{
	assert(looping_ == false);
	LOG_DEBUG("EventLoop %p of thread %d destructs in thread %d",
	          this, thread_id_, Thread::ThreadId());
	// NOTE: For file descriptor that has a channel, when its object destructs:
	// 1. Set requested event to none.
	// 2. Remove its channel.
	// 3. Close this file descriptor.
	// We must close file descriptor at last, otherwise
	// `epoll_ctl(): FATAL. operation = DEL fd = X Bad file descriptor(errno=9)`
	event_fd_channel_->set_requested_event(Channel::NONE_EVENT);
	event_fd_channel_->RemoveChannel();
	::close(event_fd_);
	t_loop_in_this_thread = nullptr;
}

void EventLoop::AssertInLoopThread()
{
	if(IsInLoopThread() == false) // Must run Loop in loop thread.
	{
		LOG_FATAL("EventLoop %p was created in thread = %d, current thread = %d",
		          this, thread_id_, Thread::ThreadId());
	}
}

void EventLoop::Loop()
{
	assert(looping_ == false); // Not in looping.
	AssertInLoopThread(); // Must run Loop in loop thread.

	looping_ = true;
	quit_ = false; // FIXME: what if someone calls quit() before loop() ?
	LOG_TRACE("EventLoop %p start looping.", this);
	// Loop forever unless quit_ is set to `true` by current loop thread or other thread.
	while(quit_ == false)
	{
		active_channel_vector_.clear(); // Clear old active channel vector.
		epoll_return_time_ = epoller_->EpollWait(-1, active_channel_vector_);
		PrintActiveChannel();
		// TODO sort channel by priority
		for(ChannelVector::iterator it = active_channel_vector_.begin();
		        it != active_channel_vector_.end();
		        ++it)
		{
			(*it)->HandleEvent(epoll_return_time_);
		}
		DoPendingFunctor(); // Do callbacks of pending_functor_vector_.
	}

	LOG_TRACE("EventLoop %p Stop looping.", this);
	looping_ = false;
}
void EventLoop::PrintActiveChannel() const
{
	for(ChannelVector::const_iterator it = active_channel_vector_.begin();
	        it != active_channel_vector_.end(); ++it)
	{
		LOG_TRACE("{%s}", (*it)->ReturnedEventToString().c_str());
	}
}
void EventLoop::DoPendingFunctor()
{
	calling_pending_functor_ = true;
	FunctorVector pending_functor; // Local variable.

	// Critical Section: Swap this empty local vector and pending_functor_vector_.
	{
		MutexLockGuard lock(mutex_);
		pending_functor.swap(pending_functor_vector_);
	}
	// We don't run each functor in critical section, instead we only swap the
	// pending_functor_vector_ with the local variable pending_functor, and
	// run each functor outside of critical section by using the local variable
	// pending_functor. Reasons:
	// 1.	Shorten the length of critical section, so we won't block other threads calling
	//		QueueInLoop(). Since we use one mutex `mutex_` to guard
	//		`pending_functor_vector_`, when we are in this critical section, the mutex_ is
	//		locked, so the threads that calls QueueInLoop() will block.
	// 2.	Avoid deadlock. Because the calling functor may call QueueInLoop() again, and
	//		in QueueInLoop(), we lock the mutex_ again, that is, we have locked a mutex,
	//		but we still request ourself mutex, this will cause a deadlock.

	for(FunctorVector::iterator it = pending_functor.begin();
	        it != pending_functor.end();
	        ++it)
	{
		(*it)();
	}
	// We don't repeat above loop until the pending_functor is empty, otherwise the loop
	// thread may go into infinite loop, can't handle IO events.
	calling_pending_functor_ = false;
}

// Quit() set quit_ to be true to terminate loop. But the actual quit happen when
// EventLoop::Loop() check `while(quit_ == false)`. If Quit() happens in other threads
// (not in loop thread), we wakeup the loop thread and it will check `while(quit_ == false)`,
// so it stops looping instantly.
void EventLoop::Quit()
{
	quit_ = true;
	// TODO: There is a chance that Loop() just executes `while(quit_ == true)` and exits,
	// then EventLoop destructs, then we are accessing an invalid object.
	// Can be fixed using mutex_ in both places.
	if(IsInLoopThread() == false)
	{
		Wakeup(); // Wakeup loop thread when we want to quit in other threads.
	}
}
// fd is readable(select: readfds argument; poll: POLLIN flag; epoll: EPOLLIN flag)
// if the counter is greater than 0.
void EventLoop::Wakeup()
{
	uint64_t one = 1;
	int write_byte = static_cast<int>(::write(event_fd_, &one, sizeof one));
	if(write_byte != sizeof one)
	{
		LOG_ERROR("EventLoop::Wakeup() write %d bytes instead of 8", write_byte);
	}
}

void EventLoop::AddOrUpdateChannel(Channel *channel)
{
	// NOTE: Only can update channel that this EventLoop owns.
	assert(channel->owner_loop() == this);
	AssertInLoopThread();
	epoller_->AddOrUpdateChannel(channel);
}
void EventLoop::RemoveChannel(Channel *channel)
{
	assert(channel->owner_loop() == this);
	AssertInLoopThread();
	epoller_->RemoveChannel(channel);
}
bool EventLoop::HasChannel(Channel *channel)
{
	assert(channel->owner_loop() == this);
	AssertInLoopThread();
	return epoller_->HasChannel(channel);
}

void EventLoop::RunInLoop(const Functor &functor)
{
	if(IsInLoopThread() == true)
	{
		functor();
	}
	else
	{
		QueueInLoop(functor);
	}
}
void EventLoop::QueueInLoop(const Functor &functor)
{
	{
		// lock is a stack variable, MutexLockGuard constructor calls `mutex_.Lock()`.
		MutexLockGuard lock(mutex_);
		pending_functor_vector_.push_back(functor); // Add this functor to functor queue.
		// lock is about to destruct, its destructor calls `mutex_.Unlock()`.
	}
	// Wakeup loop thread when either of the following conditions satisfy:
	//	1. This thread(i.e., the calling thread) is not the loop thread.
	//	2. This thread is loop thread, but now it is calling pending functor.
	//		`calling_pending_functor_` is true only in DoPendingFunctor() when
	//		we call each pending functor. When the pending functor calls QueueInLoop()
	//		again, we must Wakeup() loop thread, otherwise the newly added callbacks
	//		won't be called on time.
	// That is, only calling QueueInLoop in the EventCallback(s) of loop thread, can
	// we not Wakeup loop thread.
	if(IsInLoopThread() == false || calling_pending_functor_ == true)
	{
		Wakeup();
	}
}

// Runs callback at `time_stamp`.
TimerId EventLoop::RunAt(const TimerCallback &callback, const TimeStamp &time)
{
	return timer_queue_->AddTimer(callback, time, 0.0);
}
// Run callback after `delay` seconds.
TimerId EventLoop::RunAfter(const TimerCallback &callback, double delay)
{
	return timer_queue_->AddTimer(callback,
	                              AddTime(TimeStamp::Now(), delay),
	                              0.0);
}
// Run callback every `interval` seconds.
TimerId EventLoop::RunEvery(const TimerCallback &callback, double interval)
{
	return timer_queue_->AddTimer(callback,
	                              AddTime(TimeStamp::Now(), interval),
	                              interval);
}
void EventLoop::CancelTimer(TimerId timer_id)
{
	timer_queue_->CancelTimer(timer_id);
}
