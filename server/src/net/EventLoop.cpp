#include "EventLoop.h"
#include <sys/eventfd.h>
#include <iostream>
#include <unistd.h>
#include "Channel.h"
#include <sys/epoll.h>
#include "TcpConnection.h"

int CreateEventFd() {
	int evtFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (evtFd < 0) {
		std::cerr << "eventfd failed" << std::endl;
		abort(); // �쳣��ֹ����
	}
	return evtFd;
}

EventLoop::EventLoop():m_isLooping(false), m_isQuit(false), m_threadId(std::this_thread::get_id()),
		m_epollFd(epoll_create1(EPOLL_CLOEXEC)), m_events(16), m_wakeupFd(CreateEventFd()),
		m_wakeupChannel(new Channel(this, m_wakeupFd))
{
	if (m_epollFd < 0) {
		std::cerr << "epoll_create1 failed" << std::endl;
		abort();
	}
	// �ɶ�ʱ����HandleRead
	m_wakeupChannel->SetReadCallback(std::bind(&EventLoop::HandleRead, this));
	// ʼ�ռ��� wakeupFd �ϵĶ��¼�
	m_wakeupChannel->EnableReading();
}

EventLoop::~EventLoop()
{
	close(m_epollFd);
	close(m_wakeupFd);
}

void EventLoop::Loop()
{
	m_isLooping = true;
	m_isQuit = false;

	while (!m_isQuit) {
		std::vector<Channel*> activeChannels;
		int numEvents = epoll_wait(m_epollFd, &*m_events.begin(), static_cast<int>(m_events.size()), -1); // -1 ���޵ȴ�
		if (numEvents > 0) {
			FillActiveChannels(numEvents, &*m_events.begin(), &activeChannels); // ���¼�ע�ᵽchannel��

			if (static_cast<size_t>(numEvents) == m_events.size()) {
				m_events.resize(m_events.size() * 2);
			}
		}
		else if (numEvents == 0) { // ���¼�����

		}
		else {
			if (errno != EINTR) { // EINTR���ź��жϣ�����������������ǳ���
				std::cerr << "epoll_wait error" << std::endl;
			}
		}
		// ���������¼�
		for (Channel* channel : activeChannels) {
			channel->HandleEvent(); // ÿ��channelִ�и��ԵĻص�����
		}

		// ִ�п��߳��������
		DoPendingFunctors();
	}
	m_isLooping = false;
}

void EventLoop::Quit()
{
	m_isQuit = true;
	if (!IsInLoopThread()) {
		Wakeup();
	}
}

void EventLoop::UpdateChannel(Channel* channel)
{
	int fd = channel->GetFd();
	if (channel->GetIndex() == -1) {
		m_channels[fd] = channel; // mapӳ��
		channel->SetIndex(1);
		Update(EPOLL_CTL_ADD, channel);
	}
	else { // MOD or DEL
		if (channel->IsNoneEvent()) {
			Update(EPOLL_CTL_DEL, channel);
			channel->SetIndex(2);
		}
		else {
			Update(EPOLL_CTL_MOD, channel);
		}
	}
}

void EventLoop::RemoveChannel(Channel* channel)
{
	int fd = channel->GetFd();
	if (channel->GetIndex() == 1) {
		Update(EPOLL_CTL_DEL, channel);
	}
	channel->SetIndex(-1);
	m_channels.erase(fd); // map��ɾ�����ӳ��
}

void EventLoop::RunInLoop(Functor cb)
{
	if (IsInLoopThread()) {
		cb(); // ����ǵ�ǰ�̣߳�����ִ�лص�
	}
	else {
		QueueInLoop(cb); // ��������������
	}
}

void EventLoop::QueueInLoop(Functor cb)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex); // �ֲ���
		m_pendingFunctors.push_back(std::move(cb)); // ��������뵽���������
	}
	if (!IsInLoopThread() || m_isQuit) {
		Wakeup();
	}
}

void EventLoop::Update(int operation, Channel* channel)
{
	epoll_event event;
	event.events = channel->GetEvents();
	event.data.ptr = channel; // channel����epoll, �¼�����ʱ��ȡ��
	int fd = channel->GetFd();
	if (epoll_ctl(m_epollFd, operation, fd, &event) < 0) {
		std::cerr << "epoll_ctl() error  op=" << operation << "fd=" << fd << std::endl;
	}
}

void EventLoop::Wakeup()
{
	uint64_t one = 1;
	ssize_t n = write(m_wakeupFd, &one, sizeof one);
	if (n != sizeof one) {
		std::cerr << "Wakeup() writes " << n << " bytes" << std::endl;
	}
}



void EventLoop::HandleRead()
{
	uint64_t one = 1;
	ssize_t n = read(m_wakeupFd, &one, sizeof one);
	if (n != sizeof one) {
		std::cerr << "HandleRead() reads " << n << " bytes" << std::endl;
	}
}

void EventLoop::DoPendingFunctors()
{
	std::vector<Functor> functors;
	{
		std::lock_guard<std::mutex> lock(m_mutex); // �ֲ���
		functors.swap(m_pendingFunctors);
	}
	for (const Functor& functor : functors) {
		functor();
	}
}

void EventLoop::FillActiveChannels(int numEvents, epoll_event* events, std::vector<Channel*>* activeChannels) const
{
	for (int i = 0; i < numEvents; i++) {
		// ȡ��channel*
		Channel* channel = static_cast<Channel*>(events[i].data.ptr);
		// ����channelʵ�ʷ������¼�
		channel->SetRevents(events[i].events);
		activeChannels->push_back(channel);
	}
}
