#include "corvus/gateway/blocking_executor.h"
#include <exception>

namespace corvus::gateway
{
    namespace
    {
        thread_local const BlockingExecutor *current_executor = nullptr;
    }

    BlockingExecutor::BlockingExecutor(std::size_t threads, std::size_t max_queued)
        : max_queued_(max_queued)
    {
        workers_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i)
            workers_.emplace_back([this]
                                  { run(); });
    }

    BlockingExecutor::~BlockingExecutor()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_)
            t.join();
    }

    bool BlockingExecutor::try_submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || queue_.size() >= max_queued_)
                return false;
            queue_.push_back(std::move(task));
        }
        cv_.notify_one();
        return true;
    }

    bool BlockingExecutor::on_worker_thread() const
    {
        return current_executor == this;
    }

    std::size_t BlockingExecutor::queued() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void BlockingExecutor::run()
    {
        current_executor = this;
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]
                         { return stopping_ || !queue_.empty(); });
                if (queue_.empty())
                    return; // stopping and drained
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            try
            {
                task();
            }
            catch (...)
            {
                // Tasks report their own errors; one must never kill a worker.
            }
        }
    }

} // namespace corvus::gateway