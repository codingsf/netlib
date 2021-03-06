#ifndef NETLIB_NETLIB_TCP_CONNECTION_H_
#define NETLIB_NETLIB_TCP_CONNECTION_H_

#include <string>

#include <netlib/buffer.h>
#include <netlib/function.h>
#include <netlib/non_copyable.h>
#include <netlib/socket_address.h>

namespace netlib
{

class EventLoop;
class Socket;
class Channel;

// Interface:
// Ctor -> -HandleRead -> -HandleWrite -> -HandleClose -> -HandleError
//			-HandleRead -> -HandleClose -> -HandleError
//			-HandleWrite -> -ShutdownInLoop
// Dtor
// Getter:	loop, name, context, client_address, server_address
// Setter:	connection/message/write_complete/high_water_mark/close_callback
//				context
// Connected
// SetTcpNoDelay
// ConnectEstablished -> -set_state
// Send(const void*, int)/(const string&)/(Buffer*) -> -SendInLoop
// Shutdown -> -ShutdownInLoop.
// ForceClose -> -ForceCloseInLoop
//			-ForceCloseInLoop -> -HandleClose
// ConnectDestroyed

// TCP connection, for both client and server usage.
class TcpConnection: public NonCopyable,
	public std::enable_shared_from_this<TcpConnection>
{
public:
	// Construct with a connected socket.
	TcpConnection(EventLoop *event_loop,
	              const std::string &string_name,
	              int socket,
	              const SocketAddress &client,
	              const SocketAddress &server);
	~TcpConnection();

	// Getter.
	EventLoop *loop() const
	{
		return loop_;
	}
	const std::string &name() const
	{
		return name_;
	}
	const SocketAddress &client_address() const
	{
		return client_address_;
	}
	const SocketAddress &server_address() const
	{
		return server_address_;
	}
	void *context()
	{
		return context_;
	}

	// Setter.
	void set_context(void *context_arg)
	{
		context_ = context_arg;
	}
	void set_connection_callback(const ConnectionCallback &callback)
	{
		connection_callback_ = callback;
	}
	void set_message_callback(const MessageCallback &callback)
	{
		message_callback_ = callback;
	}
	void set_write_complete_callback(const WriteCompleteCallback &callback)
	{
		write_complete_callback_ = callback;
	}
	void set_close_callback(const CloseCallback &callback)
	{
		close_callback_ = callback;
	}
	void set_high_water_mark_callback(const HighWaterMarkCallback &callback,
	                                  int high_water_mark)
	{
		high_water_mark_callback_ = callback;
		high_water_mark_ = high_water_mark;
	}

	bool Connected() const
	{
		return state_ == CONNECTED;
	}
	void SetTcpNoDelay(bool on);
	void ConnectEstablished();
	void Send(const void *data, int length);
	void Send(const std::string &string_data);
	void Send(Buffer *buffer_data); // TODO: Swap!
	void Shutdown();
	void ForceClose();
	void ConnectDestroyed();

private:
	enum State
	{
		CONNECTING,
		CONNECTED,
		DISCONNECTING,
		DISCONNECTED
	};
	void set_state(State state)
	{
		state_ = state;
	}
	const char *StateToCString() const;

	void HandleRead(const TimeStamp &receive_time);
	void HandleWrite();
	void HandleClose();
	void HandleError();

	void ShutdownInLoop();
	void SendInLoop(const char *data, int length);
	void ForceCloseInLoop();

	EventLoop *loop_;
	const std::string name_;
	State state_; // FIXME: Atomic.
	void *context_; // TODO: use struct encapsulate it.
	std::unique_ptr<Socket> socket_; // connected_socket
	std::unique_ptr<Channel> channel_;
	const SocketAddress client_address_;
	const SocketAddress server_address_;
	Buffer input_buffer_;
	Buffer output_buffer_;
	ConnectionCallback connection_callback_;
	MessageCallback message_callback_;
	WriteCompleteCallback write_complete_callback_;
	CloseCallback close_callback_; // TcpServer/TcpClient::RemoveConnection().
	HighWaterMarkCallback high_water_mark_callback_;
	int high_water_mark_;
	static const int kInitialHighWaterMark = 64 * 1024 * 1024; // 64KB
};

void DefaultConnectionCallback(const TcpConnectionPtr&);
void DefaultMessageCallback(const TcpConnectionPtr&, Buffer*, const TimeStamp&);

}

#endif // NETLIB_NETLIB_TCP_CONNECTION_H_
