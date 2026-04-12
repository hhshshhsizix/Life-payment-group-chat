#pragma once

#include "Callbacks.h"
#include "Buffer.h"
#include "Channel.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include <any>
#include <openssl/ssl.h>
#include <openssl/err.h>


class TcpConnection : public std::enable_shared_from_this<TcpConnection>
{
public:
	TcpConnection(EventLoop* loop, int sock, const InetAddress& serverAddr, const InetAddress& clientAddr);
	~TcpConnection();
public:

	void setSSL(SSL* ssl) { m_ssl = ssl; }
    SSL* getSSL() const { return m_ssl; }

	void StartTlsHandshake();

	EventLoop* GetLoop() const { return m_loop; }

	const InetAddress& GetServerAddress() const { return m_sAddr; }
	const InetAddress& GetClientAddress() const { return m_cAddr; }

	bool IsConnected() const { return m_state == Connected; }

	void SendMessage(const std::string& message);
	void ShutDown(); // �����ر�����

	void SetConnectionCallback(const ConnectionCallback& cb) {
		m_connectionCb = cb;
	}
	void SetRecvMessageCallback(const RecvMessageCallback& cb) {
		m_messageCb = cb;
	}
	void SetWriteOverCallback(const WriteOverCallback& cb) {
		m_writeOverCb = cb;
	}
	void SetCloseCallback(const CloseCallback& cb) {
		m_closeCb = cb;
	}

	// ��channelע�ᵽeventloop��
	void CreateConnect();
	// ��channel��eventloop���Ƴ�
	void DestroyConnect();

	void setContext(const std::any& context) { m_context = context; }
	const std::any& getContext() const { return m_context; }
	bool hasContext() const { return m_context.has_value(); }

	std::string& getChatCache() { return m_chatCache; } // 提供缓存访问接口
private:
	enum STATE {
		DisConnected, // δ����
		Connecting, // ��������
		Connected, // ������
		DisConnecting // ���ڹر�����
	};

	void SetState(STATE s) { m_state = s; }

	void HandleRead();
	void HandleWrite();
	void HandleClose();
	void HandleError();

	void SendInLoop(const std::string& message);
	void ShutdownInLoop();

private:

	SSL* m_ssl = nullptr;

	EventLoop* m_loop;
	const int m_cliSock; // ����Ŀͻ���socket
	const InetAddress m_sAddr; // ��������ַ
	const InetAddress m_cAddr; // �ͻ��˵�ַ

	std::any m_context; // ���û��� string

	std::atomic<STATE> m_state;

	Buffer m_inputBuffer; // ���ջ�����
	Buffer m_outputBuffer; // ���ͻ�����

	std::unique_ptr<Channel> m_channel;

	// �û��Ļص�
	ConnectionCallback m_connectionCb;
	RecvMessageCallback m_messageCb;
	WriteOverCallback m_writeOverCb;

	// �ڲ��ص�
	CloseCallback m_closeCb; // ����֪ͨTcpServer�Ƴ�������

	std::string m_chatCache;
};

