#include "TcpConnection.h"
#include <unistd.h>
#include <iostream>
#include <cassert>
#include <string.h>
#include "UserManager.h"

TcpConnection::TcpConnection(EventLoop* loop, int sock, const InetAddress& serverAddr, const InetAddress& clientAddr)
	:m_loop(loop), m_cliSock(sock), m_sAddr(serverAddr), m_cAddr(clientAddr), m_state(Connecting), m_channel(new Channel(loop, sock))
	,m_ssl(nullptr)
{
	m_channel->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
	m_channel->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
	m_channel->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
	m_channel->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

TcpConnection::~TcpConnection()
{
	if (m_ssl) {
		SSL_shutdown(m_ssl); // 关闭TLS连接
		SSL_free(m_ssl);     // 释放SSL对象
		m_ssl = nullptr;
    }
	close(m_cliSock);
}

void TcpConnection::SendMessage(const std::string& message)
{
	if (m_state != Connected) return;

	// ������ڵ�ǰ�߳�
	if (m_loop->IsInLoopThread()) {
		SendInLoop(message);
	}
	else {
		// ������̳߳ص��õģ������������EventLoopִ��
		m_loop->RunInLoop(std::bind(&TcpConnection::SendInLoop, this, message));
	}
}

void TcpConnection::ShutDown()
{
	if (m_state == Connected) {
		SetState(DisConnecting);
		m_loop->RunInLoop(std::bind(&TcpConnection::ShutdownInLoop, this));
	}
}

void TcpConnection::HandleRead() 
{
if (!m_ssl) {
        // 非 TLS 连接：走原有明文逻辑，完全不变
        ssize_t len = m_inputBuffer.ReadFd(m_cliSock);
        if (len > 0) {
            if (m_messageCb) {
                m_messageCb(shared_from_this(), &m_inputBuffer);
            }
        } else if (len == 0) {
            HandleClose();
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            perror("read error");
            HandleError();
        }
        return;
    }

        if (m_state != Connected) {
        int ret = SSL_do_handshake(m_ssl);
        if (ret <= 0) {
            int err = SSL_get_error(m_ssl, ret);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                // 握手需要等待，直接返回，等下一次事件
                return;
            } else {
                std::cerr << "TLS handshake failed: " << ERR_error_string(ERR_get_error(), nullptr) << std::endl;
                HandleError();
                return;
            }
        }
        // 握手成功，标记状态
        std::cout << "TLS handshake succeeded for fd=" << m_cliSock << std::endl;
        m_state = Connected;
    }

	char buf[4096];
    ssize_t len = SSL_read(m_ssl, buf, sizeof(buf));
    if (len > 0) {
        m_inputBuffer.Append(buf, len);
        if (m_messageCb) {
            m_messageCb(shared_from_this(), &m_inputBuffer);
        }
    } else if (len == 0) {
        // TLS 正常关闭
        HandleClose();
    } else {
        int err = SSL_get_error(m_ssl, static_cast<int>(len));
        switch (err) {
            case SSL_ERROR_NONE:
            case SSL_ERROR_WANT_READ:
            case SSL_ERROR_WANT_WRITE:
                return;
            case SSL_ERROR_ZERO_RETURN:
                HandleClose();
                return;
            default:
                std::cerr << "TLS read error: " << ERR_error_string(err, nullptr) << std::endl;
                HandleError();
                return;
        }
    }
}

void TcpConnection::HandleWrite()
{
	if (m_channel->IsWriting())
    {
        //  TLS 加密分支：用 SSL_write 替代原生 write
        if (m_ssl != nullptr)
        {

            if (m_state != Connected) return;

            ssize_t len = SSL_write(
                m_ssl,
                m_outputBuffer.peek(),
                static_cast<int>(m_outputBuffer.ReadableBytes())
            );

            if (len > 0)
            {
                m_outputBuffer.rm_ReadIndex(len);
                if (m_outputBuffer.ReadableBytes() == 0)
                {
                    m_channel->DisableWriting();
                    if (m_writeOverCb)
                    {
                        m_writeOverCb(shared_from_this());
                    }
                }
                if (m_state == DisConnecting)
                {
                    ShutdownInLoop();
                }
            }
            else
            {
                int err = SSL_get_error(m_ssl, static_cast<int>(len));
                // 非阻塞握手/发送，直接返回，继续监听事件
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE)
                {
                    char errBuf[256];
                    ERR_error_string_n(err, errBuf, sizeof(errBuf));
                    std::cerr << "SSL write error: " << errBuf << std::endl;
                    HandleError();
                }
            }
        }
        // 📡 明文分支兼容非 TLS 连接
        else
        {
            ssize_t len = write(m_cliSock, m_outputBuffer.peek(), m_outputBuffer.ReadableBytes());
            if (len > 0)
            {
                m_outputBuffer.rm_ReadIndex(len);
                if (m_outputBuffer.ReadableBytes() == 0)
                {
                    m_channel->DisableWriting();
                    if (m_writeOverCb)
                    {
                        m_writeOverCb(shared_from_this());
                    }
                }
                if (m_state == DisConnecting)
                {
                    ShutdownInLoop();
                }
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    std::cout << "TcpConnection::HandleWrite() write error" << std::endl;
                    HandleError();
                }
            }
        }
    }
}

void TcpConnection::HandleClose()
{
	SetState(DisConnected);
	m_channel->DisableAll();

	// ����һ��shared_ptr�����Լ�����ֹ�ص�ʱ�Լ����ݻ�
	TcpConnectionPtr guardThis(shared_from_this());

	if (m_connectionCb) {
		m_connectionCb(guardThis);
	}
	if (m_closeCb) {
		m_closeCb(guardThis); // TcpServer���Ƴ����ӻص�
	}
	UserManager::getInstance().RemoveConnection(shared_from_this());
}

void TcpConnection::HandleError()
{
	int err;
	socklen_t len = sizeof err;
	if (getsockopt(m_cliSock, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
		err = errno;
	}
	std::cerr << "TcpConnection::HandleError() [" << m_cAddr.ToIpPort() << "] - " << strerror(err) << std::endl;
}

void TcpConnection::SendInLoop(const std::string& message)
{
	ssize_t send_len = 0;
	ssize_t remaining = message.size(); // ʣ���
	bool faultError = false;
	
	if (m_outputBuffer.ReadableBytes() == 0) { // 
		
		if (m_ssl != nullptr) 
		{
            // ---------------- TLS 加密发送 ----------------
            send_len = SSL_write(m_ssl, message.data(), (int)message.size());

            if (send_len > 0) {
                remaining = message.size() - send_len;
                if (remaining == 0 && m_writeOverCb) {
                    m_loop->QueueInLoop(std::bind(m_writeOverCb, shared_from_this()));
                }
            } else {
                send_len = 0;
                int err = SSL_get_error(m_ssl, (int)send_len);
                if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                    std::cerr << "TcpConnection::SendInLoop() SSL write error" << std::endl;
                    faultError = true;
                }
            }
        }
		else {
			// ֱ�ӷ�������Ҫ����
			send_len = write(m_cliSock, message.data(), message.size());
			if (send_len >= 0) {
				remaining = message.size() - send_len;
				if (remaining == 0 && m_writeOverCb) {
					// ���һ���Է����ˣ����÷�����ϵĻص�
					m_loop->QueueInLoop(std::bind(m_writeOverCb, shared_from_this()));
				}
			}
			else {
				send_len = 0;
				if (errno != EWOULDBLOCK) { // ������ǻ����������µ�
					std::cerr << "TcpConnection::SendInLoop() write error" << std::endl;
					faultError = true;
				}
			}
		}
	}
	if (!faultError && remaining > 0) {

		m_outputBuffer.Append(message.data() + send_len, remaining);


		if (!m_channel->IsWriting()) {
			m_channel->EnableWriting();
		}
	}

	struct sockaddr_in peerAddr;
	socklen_t peerAddrLen = sizeof(peerAddr);
	if (getpeername(m_cliSock, (struct sockaddr*)&peerAddr, &peerAddrLen) == 0) {
		int peerPort = ntohs(peerAddr.sin_port);

		std::cout << "[Server Debug] fd=" << m_cliSock << " port is : " << peerPort << std::endl;
	}
}

void TcpConnection::ShutdownInLoop()
{
	// ֻ��û���ݿ�дʱ���Ż�ر�д��
	if (m_outputBuffer.ReadableBytes() == 0) {
		::shutdown(m_cliSock, SHUT_WR);
	}
	// �������û���꣬HandleWrite���ڷ������״̬��������ShutdownInLoop
}

void TcpConnection::CreateConnect()
{
	SetState(Connected);
	m_channel->Tie(shared_from_this());

	m_channel->EnableReading(); // ��ʼ�������¼�

	// �����û����õ����ӽ����ص�
	if (m_connectionCb) {
		m_connectionCb(shared_from_this());
	}
}

void TcpConnection::DestroyConnect()
{
	if (m_state == Connected) {
		SetState(DisConnected);
		m_channel->DisableAll(); // ��EventLoop���Ƴ����м���
	}
	m_channel->RemoveFormEventLoop();
}

void TcpConnection::StartTlsHandshake()
{
    if (!m_ssl) return;

    // 只做初始化，绑定 socket，设置为服务端模式
    SSL_set_fd(m_ssl, m_cliSock);
    SSL_set_accept_state(m_ssl);

    // 触发第一次握手，后续交给 HandleRead 持续重试
    SSL_do_handshake(m_ssl);
    m_channel->EnableReading();
}