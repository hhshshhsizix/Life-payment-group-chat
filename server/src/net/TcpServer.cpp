#include "TcpServer.h"
extern SSL_CTX* g_ssl_ctx;
TcpServer::TcpServer(EventLoop* loop, const InetAddress& cliAddr, bool reusePort)
	: m_loop(loop), m_acceptor(new Acceptor(loop, cliAddr, reusePort))
	, m_threadPool(std::make_shared<ThreadPool>(0)) // ��ʼ��0���߳�
	, m_connectionId(1)
{
	m_acceptor->SetNewConnectionCallback(std::bind(&TcpServer::NewConnection, this, std::placeholders::_1, std::placeholders::_2));
} 

TcpServer::~TcpServer()
{
	// ȷ���������Ӷ����Ƴ�
	for (auto& item : m_connections) {
		TcpConnectionPtr conn = item.second;
		item.second.reset(); // �Ͽ�shared_ptr
		m_loop->RunInLoop(std::bind(&TcpConnection::DestroyConnect, conn));
	}
}

void TcpServer::Start()
{
	//if (m_threadPool) m_threadPool->Start();

	if (m_loopPool)
    m_loopPool->start(); // 启动从Reactor

	m_loop->RunInLoop(std::bind(&Acceptor::Listen, m_acceptor.get()));
}

void TcpServer::SetThreadsNum(int numThreads)
{
	m_loopPool = std::make_unique<EventLoopThreadPool>(m_loop, numThreads);

	//m_threadPool = std::make_shared<ThreadPool>(numThreads);
}

void TcpServer::NewConnection(int cliSock, const InetAddress& cliAddr)
{
	std::string cliName = cliAddr.ToIpPort() + "#" + std::to_string(m_connectionId++);

	InetAddress localAddr(0);

	EventLoop* ioLoop = m_loopPool->getNextLoop();
	
	TcpConnectionPtr conn = std::make_shared<TcpConnection>(m_loop, cliSock, localAddr, cliAddr);

	if (m_enableTls)   //  聊天服务器：开启 TLS
		{
			SSL* ssl = SSL_new(g_ssl_ctx);
			conn->setSSL(ssl);
			conn->StartTlsHandshake();
		}
		else               //  HTTP 服务器：绝对不开启 TLS
		{
			conn->setSSL(nullptr);
		}
	
	m_connections[cliName] = conn; // TcpServer��¼����ͻ���
	std::cout << "user[" << cliName << "]is ADD. Current Size:" << m_connections.size() << std::endl;
	conn->SetConnectionCallback(m_connectionCb);
	conn->SetRecvMessageCallback(m_recvMessageCb);
	conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

	// ע��epoll
	ioLoop->RunInLoop(std::bind(&TcpConnection::CreateConnect, conn));
}

void TcpServer::RemoveConnection(const TcpConnectionPtr& conn)
{
	m_loop->RunInLoop(std::bind(&TcpServer::RemoveConnectionInLoop, this, conn));
}

void TcpServer::RemoveConnectionInLoop(const TcpConnectionPtr& conn)
{
	for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
		if (it->second == conn) {
			// �ҵ��ˣ�ɾ��
			std::cout << "user[" << it->first << "]is DEL" << std::endl;
			m_connections.erase(it);
			break;
		}
	}
	m_loop->QueueInLoop(std::bind(&TcpConnection::DestroyConnect, conn));
	// ҵ����Ƴ�
	UserManager::getInstance().RemoveConnection(conn);
}

