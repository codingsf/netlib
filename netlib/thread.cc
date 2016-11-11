#include <netlib/thread.h>

#include <assert.h> // assert()
#include <sys/syscall.h> // syscall()
#include <unistd.h> // syscall()

using std::move;
using std::atomic;
using netlib::Thread;

namespace netlib
{

namespace detail
{

struct ThreadData // Store the arguments that need passed to pthread_create.
{
	using ThreadFunction = netlib::Thread::ThreadFunction;

	ThreadFunction function_; // Thread's start function.
	pid_t &thread_id_;

	ThreadData(const ThreadFunction &function, pid_t &thread_id)
		: function_(function),
		  thread_id_(thread_id)
	{}

	void RunInThread()
	{
		// 1. Set thread id.
		thread_id_ = Thread::ThreadId();
		// 2. Call thread work function.
		function_();
	}
};

void *StartThread(void *object) // Thread start function passed to pthread_create.
{
	ThreadData *data = static_cast<ThreadData*>(object);
	data->RunInThread();
	delete data;
	return nullptr;
}

}

}

using netlib::Thread;
using netlib::detail::ThreadData;

// Every thread has it own instance of __thread variable.
__thread pid_t t_cached_thread_id = 0; // The thread id in the kernel, not the pthread_t.

// The child fork handler for pthread_atfork().
// Since we cache the thread id in t_cached_thread_id, after fork(2), the child
// process can see the cached thread id, but it is not its own thread id. Thus, we
// empty the cached thread id for child process and re-cache the right thread id
// in the child fork handler before returning from fork(2),
void ChildForkHandler()
{
	t_cached_thread_id = 0;
	Thread::ThreadId();
}

class ThreadIdInitializer
{
public:
	ThreadIdInitializer()
	{
		Thread::ThreadId();
		// #include <pthread.h>
		// int pthread_atfork(void(*prepare)(void), void(*parent)(void), void(*child)(void));
		// Return 0 if OK, error number on failure.
		// The child fork handler is called in the context of the child process
		// before returning from fork.
		assert(pthread_atfork(NULL, NULL, &ChildForkHandler) == 0);
	}
};

ThreadIdInitializer thread_id_initializer_object; // Global object.

atomic<int32_t> Thread::created_number_(0);

Thread::Thread(const ThreadFunction &function)
	: started_(false),
	  joined_(false),
	  pthread_id_(0),
	  thread_id_(0),
	  function_(function)
{
	++created_number_;
}

Thread::Thread(ThreadFunction &&function)
	: started_(false),
	  joined_(false),
	  pthread_id_(0),
	  thread_id_(0),
	  function_(move(function))
{
	++created_number_;
}

Thread::~Thread()
{
	if(started_ == true && joined_ == false)
	{
		// By default, a thread's termination status is retained until we call pthread_join
		// for that thread. A thread's underlying storage can be reclaimed immediately
		// on termination if the thread has been detached. After a thread is detached,
		// we can't use pthread_join to wait for its termination status, because calling
		// pthread_join for a detached thread results in undefined behavior.
		//```
		// #include <pthread.h>
		// int pthread_detach(pthread_t tid);
		// Return 0 if OK, error number on failure.
		//```
		pthread_detach(pthread_id_); // TODO: why not pthread_exit()?
	}
}

int Thread::Join()
{
	assert(started_ == true && joined_ == false);
	joined_ = true;

	// If any thread within a process calls exit, _Exit, or _exit, then the entire process
	// terminates. A single thread can exit in three ways to stop its flow of control,
	// without terminating the entire process.
	//  1. Return from the start routine. The return value is the thread's exit code.
	//  2. Be canceled by another thread in the same process.
	//  3. Call pthread_exit.
	// ```
	// #include <pthread.h>
	// void pthread_exit(void *rval_ptr);
	// ```
	// rval_ptr is available to other threads in the process by calling pthread_join().
	//
	// ```
	// #include <pthread.h>
	// int pthread_join(pthread_t thread, void **rval_ptr);
	// Return 0 if OK, error number on failure
	// ```
	// The calling thread will block until the specified thread calls pthread_exit,
	// returns from its start routine, or is canceled. If the thread returned from its
	// start routine, rval_ptr will contain the return code. If the thread was canceled,
	// the memory location specified by rval_ptr is set to PTHREAD_CANCELED.
	// By calling pthread_join, we place the thread with which we're joining in the
	// detached state so that its resources can be recovered. If the thread was already
	// in the detached state, pthread_join can fail with returning EINVAL.
	// If we're not interested in a thread's return value, we can set rval_ptr to NULL.
	// In this case, calling pthread_join allows us to wait for the specified thread,
	// but does not retrieve the thread's termination status.
	return pthread_join(pthread_id_, NULL);
}

void Thread::Start()
{
	assert(started_ == false);
	started_ = true;
	// TODO: move(function_)
	ThreadData *data = new ThreadData(function_, thread_id_);
	// int pthread_create(pthread_t *ptid, const pthread_attr_t *attr,
	//                    void* (*fun) (void*), void *arg);
	// Return: 0 if OK, error number on failure.
	// *ptid is set to the thread ID of the newly created thread.
	// attr = NULL: create a thread with the default attributes.
	// The newly created thread starts running at the address of the fun function.
	// arg is the argument that passed to fun. If we need pass more than one argument,
	// we should store them in a structure and pass the address of the structure in arg.
	if(pthread_create(&pthread_id_, NULL, &detail::StartThread, data) != 0)
	{
		started_ = false;
		delete data;
		// TODO: how to use different log LEVEL???
		// LOG_FATAL("Failed in pthread_create");
	}
}

pid_t Thread::ThreadId() // Return the cached thread-id.
{
	if(t_cached_thread_id == 0) // If not cached yet.
	{
		t_cached_thread_id = static_cast<pid_t>(::syscall(SYS_gettid));
	}
	return t_cached_thread_id;
}
