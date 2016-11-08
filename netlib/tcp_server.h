#ifndef NETLIB_NETLIB_TCP_SERVER_H_
#define NETLIB_NETLIB_TCP_SERVER_H_

#include <map>
#include <string>
#include <memory>

#include <netlib/non_copyable.h>
#include <netlib/callback.h>

namespace netlib
{

class EventLoop;
class EventLoopThreadPool;
class Acceptor;
class SocketAddress;

// TcpServer class's task: Manage the tcp connections get by accept(2).
// This class is used directly by user and its lifetime is controlled by user.
// User only needs set callback and then call Start().
class TcpServer: public NonCopyable
{
public:
	TcpServer(EventLoop *loop, const SocketAddress &listen_address);
	~TcpServer(); // Force outline destructor, for unique_ptr members.

	// Set connection callback. Not thread safe.
	void set_connection_callback(const ConnectionCallback &callback)
	{
		connection_callback_ = callback;
	}
	// Set message callback. Not thread safe.
	void set_message_callback(const MessageCallback &callback)
	{
		message_callback_ = callback;
	}
	void set_write_complete_callback(const WriteCompleteCallback &callback)
	{
		write_complete_callback_ = callback;
	}

	// Always accept new connection in loop's thread. Must be called before starting.
	// thread_number =
	//	(1) 0:	All I/O in loop's thread, no thread will be created. This is the default value.
	//	(2) 1:	All I/O in another thread.
	//	(3) N:	Create a thread pool of N threads, new connections are assigned
	//				on a round-robin basis.
	void SetThreadNumber(int thread_number);

	// Start the server if it's not listening. It's harmless to call it multiple times.
	// Thread safe.
	void Start();

private:
	// TcpConnectionPtr is `shared_ptr<TcpConnection>`
	using ConnectionMap = std::map<std::string, TcpConnectionPtr>;

	// Not thread safe, but in loop.
	// Create the TcpConnection object, add it to the connection map,
	// set callbacks, and call `connection_object->ConnectionEstablished()`, which
	// calls the user's ConnectionCallback.
	// NewConnectionCallback = std::function<void(int, const SocketAddress&)>;
	// which is used in only Acceptor class, so we don't put it in the callback.h.
	void HandleNewConnection(int socket_fd, const SocketAddress &peer_address);
	// Thread safe.
	void RemoveConnection(const TcpConnectionPtr &connection);
	// Not thread safe, but in loop.
	void RemoveConnectionInLoop(const TcpConnectionPtr &connection);

	EventLoop *loop_; // The acceptor loop.
	// listen_address.ToHostPort(), i.e., "IP_address:Port" representation of listen address.
	const std::string name_;
	// Use Acceptor to get the new connection's socket file descriptor.
	// When new connection arrives, acceptor_ calls HandleNewConnection().
	std::unique_ptr<Acceptor> acceptor_; // Avoid exposing Acceptor.
	std::unique_ptr<EventLoopThreadPool> thread_pool_;
	// TcpServer stores the user's *callback and pass them to the TcpConnection
	// when creating the TcpConnection object.
	// std::function<void(const TcpConnectionPtr&)>;
	ConnectionCallback connection_callback_;
	// std::function<void(const TcpConnectionPtr&, const char*, int)>;
	MessageCallback message_callback_;
	WriteCompleteCallback write_complete_callback_;
	bool started_;
	// Start from 1, increase by 1 each time a new connection is established. It is used
	// to identify different TcpConnection objects since one TcpServer may create many
	// TcpConnection objects. Always in loop thread.
	int next_connection_id_;
	// std::map<std::string, TcpConnectionPtr>;
	// Every TcpConnection object has a name that is generated by its owner TcpServer
	// when creating it: connection_name = name_ + "#next_connection_id_".
	// connection_name is the key of connection_map_.
	ConnectionMap connection_map_;
};

}

#endif // NETLIB_NETLIB_TCP_SERVER_H_