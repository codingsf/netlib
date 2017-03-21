#ifndef NETLIB_NETLIB_EVENT_LOOP_THREAD_POOL_H_
#define NETLIB_NETLIB_EVENT_LOOP_THREAD_POOL_H_

#include <vector>
#include <functional>

#include <netlib/non_copyable.h>

namespace netlib
{

class EventLoop;
class EventLoopThread;

// Review: all

// Interface:
// Ctor
// Start
// GetNextLoop

class EventLoopThreadPool: public NonCopyable
{
public:
	using InitialTask = std::function<void(EventLoop*)>;

	explicit EventLoopThreadPool(EventLoop *base_loop,
	                             const InitialTask &initial_task = InitialTask(),
	                             const int thread_number = 0);
	void Start();
	EventLoop *GetNextLoop();

private:
	EventLoop *base_loop_;
	InitialTask initial_task_;
	bool started_;
	const int thread_number_;
	std::vector<EventLoopThread*> thread_pool_; // No need. Should be deleted.
	std::vector<EventLoop*> loop_pool_;
	int next_loop_index_; // Always in loop thread.
};

}

#endif // NETLIB_NETLIB_EVENT_LOOP_THREAD_POOL_H_
