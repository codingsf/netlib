#include <netlib/channel.h>

#include <sys/epoll.h> // EPOLL*

#include <netlib/event_loop.h> // EventLoop
#include <netlib/logging.h> // Log

using std::string;
using std::shared_ptr;
using netlib::EventLoop;
using netlib::Channel;

// EPOLLIN
// fd is available for read(2): data other than high-priority data can be read.
// EPOLLPRI
// There is urgent data available for read(2): high-priority data can be read.
// EPOLLRDHUP
// Stream socket peer closes connection, or shut down writing half of connection.
// This flag is useful to detect peer shutdown when using Edge Triggered.
// EPOLLOUT
// fd is available for write(2): normal data can be read.
// EPOLLET
// Set the Edge Triggered behavior for fd. Default is Level Triggered.
// EPOLLONESHOT
// Set the one-shot behavior for the fd(Disable monitoring after event notification).
// After an event is pulled out with epoll_wait(2), the associated fd is disabled and
// no other events will be reported by the epoll interface. The user must call epoll_ctl()
// with EPOLL_CTL_MOD to rearm the fd with a new event mask.
// EPOLLERR
// Error condition happened on the fd. epoll_wait(2) always wait for this event,
// it is not necessary to set it in events.
// EPOLLHUP
// Hang up happened on the fd. epoll_wait(2) always wait for this event;
// it is not necessary to set it in events.
const int kNoneEvent = 0;
const int kReadEvent = EPOLLIN | EPOLLPRI | EPOLLRDHUP;
const int kWriteEvent = EPOLLOUT;
const int kCloseEvent = EPOLLHUP;
const int kErrorEvent = EPOLLERR;

// declaration of ‘fd’ shadows a member of 'this' [-Werror=shadow]
Channel::Channel(EventLoop *loop, int file_descriptor):
	owner_loop_(loop),
	fd_(file_descriptor),
	requested_event_(kNoneEvent),
	returned_event_(kNoneEvent),
	state_in_epoller_(-1), // Epoller::kRaw.
	tied_(false),
	event_handling_(false),
	added_to_loop_(false)
{}

Channel::~Channel()
{
	assert(event_handling_ == false);
	assert(added_to_loop_ == false);
	if(owner_loop_->IsInLoopThread() == true)
	{
		assert(owner_loop_->HasChannel(this) == false);
	}
}

void Channel::set_requested_event(RequestedEventType type)
{
	switch(type)
	{
	case READ_EVENT:
		requested_event_ |= kReadEvent;
		break;
	case NOT_READ:
		requested_event_ &= ~kReadEvent;
		break;
	case WRITE_EVENT:
		requested_event_ |= kWriteEvent;
		break;
	case NOT_WRITE:
		requested_event_ &= ~kWriteEvent;
		break;
	case NONE_EVENT:
		requested_event_ = kNoneEvent;
	}
	AddOrUpdateChannel();
}
void Channel::AddOrUpdateChannel()
{
	added_to_loop_ = true;
	owner_loop_->AddOrUpdateChannel(this);
	// Invoke `void Epoller::AddOrUpdateChannel(Channel*)`
}

void Channel::set_tie(const shared_ptr<void> &object)
{
	tie_ = object;
	tied_ = true;
}
void Channel::set_event_callback(EventCallbackType type, const EventCallback &callback)
{
	switch(type)
	{
	case READ_CALLBACK:
		read_callback_ = callback;
		break;
	case WRITE_CALLBACK:
		write_callback_ = callback;
		break;
	case CLOSE_CALLBACK:
		close_callback_ = callback;
		break;
	case ERROR_CALLBACK:
		error_callback_ = callback;
	}
}

bool Channel::IsRequestedArgumentEvent(RequestedEventType type)
{
	switch(type)
	{
	case READ_EVENT:
		return requested_event_ & kReadEvent;
	case WRITE_EVENT:
		return requested_event_ & kWriteEvent;
	case NONE_EVENT:
		return requested_event_ == kNoneEvent;
	default:
		return true;
	}
}

void Channel::HandleEvent(TimeStamp receive_time)
{
	if(tied_ == true)
	{
		shared_ptr<void> guard = tie_.lock();
		if(guard)
		{
			HandleEventWithGuard(receive_time);
		}
	}
	else
	{
		HandleEventWithGuard(receive_time);
	}
}
// Call different callbacks based on the value of returned_event_.
// Invoked by EventLoop::Loop().
void Channel::HandleEventWithGuard(TimeStamp receive_time)
{
	event_handling_ = true;
	LOG_TRACE("%s", ReturnedEventToString().c_str());

	if((returned_event_ & kReadEvent) && read_callback_)
	{
		read_callback_(receive_time);
	}
	if((returned_event_ & kWriteEvent) && write_callback_)
	{
		write_callback_(receive_time);
	}
	if((returned_event_ & kCloseEvent) &&
	        !(returned_event_ & EPOLLIN) &&
	        close_callback_)
	{
		close_callback_(receive_time);
	}
	if((returned_event_ & kErrorEvent ) && error_callback_)
	{
		error_callback_(receive_time);
	}
	event_handling_ = false;
}

void Channel::RemoveChannel()
{
	assert(IsRequestedArgumentEvent(NONE_EVENT) == true);
	added_to_loop_ = false;
	owner_loop_->RemoveChannel(this);
}

string Channel::RequestedEventToString() const
{
	return EventToString(fd_, requested_event_);
}

string Channel::ReturnedEventToString() const
{
	return EventToString(fd_, returned_event_);
}

string Channel::EventToString(int fd, int event)
{
	char buffer[32] = "";
	char *ptr = buffer, *buffer_end = buffer + sizeof buffer;
	ptr += snprintf(ptr, buffer_end - ptr, "%d: ", fd);
	if(event & EPOLLIN)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "IN ");
	}
	if(event & EPOLLPRI)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "PRI ");
	}
	if(event & EPOLLOUT)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "OUT ");
	}
	if(event & EPOLLHUP)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "HUP ");
	}
	if(event & EPOLLRDHUP)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "RDHUP ");
	}
	if(event & EPOLLERR)
	{
		ptr += snprintf(ptr, buffer_end - ptr, "%s", "ERR ");
	}
	return buffer;
}
