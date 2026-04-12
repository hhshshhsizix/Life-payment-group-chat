#pragma once
#include <vector>
#include <memory>
#include <thread>
#include "EventLoop.h"

class EventLoopThreadPool
{
public:
    EventLoopThreadPool(EventLoop* baseLoop, int numThreads);
    void start();
    EventLoop* getNextLoop();

private:
    EventLoop* baseLoop_;
    int numThreads_;
    int next_;

    std::vector<EventLoop*> loops_;
    std::vector<std::unique_ptr<EventLoop>> loopsOwn_;
    std::vector<std::thread> threads_;
};