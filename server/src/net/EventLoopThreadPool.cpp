#include "EventLoopThreadPool.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop, int numThreads)
    : baseLoop_(baseLoop),
      numThreads_(numThreads),
      next_(0)
{
}

void EventLoopThreadPool::start()
{
    for (int i = 0; i < numThreads_; ++i)
    {
        auto loop = std::make_unique<EventLoop>();
        loops_.push_back(loop.get());

        std::thread t(std::bind(&EventLoop::Loop, loop.get()));

        threads_.push_back(std::move(t));
        loopsOwn_.push_back(std::move(loop));
    }
}

EventLoop* EventLoopThreadPool::getNextLoop()
{
    if (loops_.empty())
        return baseLoop_;

    EventLoop* loop = loops_[next_];
    next_ = (next_ + 1) % loops_.size();
    return loop;
}