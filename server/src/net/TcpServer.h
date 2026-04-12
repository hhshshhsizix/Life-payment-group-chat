#pragma once

#include "Callbacks.h"
#include "TcpConnection.h"
#include "Acceptor.h"
#include "EventLoop.h"
#include "ThreadPool.h"
#include "UserManager.h"
#include "EventLoopThreadPool.h"
#include <unistd.h>
class TcpServer
{
public:
	TcpServer(EventLoop* loop, const InetAddress& cliAddr, bool reusePort = false);
	~TcpServer();
public:
	void Start();
	// ��Start֮ǰ����
	void SetThreadsNum(int numThreads);

	void SetRecvMessageCallback(const RecvMessageCallback& cb) {
		m_recvMessageCb = cb;
	}
	void SetConnectionCallback(const ConnectionCallback& cb) {
		m_connectionCb = cb;
	}

	void enableTls() { m_enableTls = true; }
private:
	// ��Acceptor����
	void NewConnection(int cliSock, const InetAddress& cliAddr);
	// ��TcpConnection����
	void RemoveConnection(const TcpConnectionPtr& conn);
	// �� EventLoop����
	void RemoveConnectionInLoop(const TcpConnectionPtr& conn);
private:
	EventLoop* m_loop;

	ConnectionCallback m_connectionCb;
	RecvMessageCallback  m_recvMessageCb;

	std::unique_ptr<Acceptor> m_acceptor;

	std::shared_ptr<ThreadPool> m_threadPool;
	std::unique_ptr<EventLoopThreadPool> m_loopPool;

	std::map<std::string, TcpConnectionPtr> m_connections;
	int m_connectionId;

	bool m_enableTls = false;
};

