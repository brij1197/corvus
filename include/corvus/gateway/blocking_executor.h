#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace corvus::gateway
{
    class BlockingExecutor
    {
    public:
        BlockingExecutor(std::size_t threads, std::size_t max_queued);
        ~BlockingExecutor();

        BlockingExecutor(const BlockingExecutor &) = delete;
        BlockingExecutor &operator=(const BlockingExecutor &) = delete;

        bool try_submit(std::function<void()> task);

        bool on_worker_thread() const;

        std::size_t threads() const noexcept { return workers_.size(); }
        std::size_t queued() const;

    private:
        void run();

        const std::size_t max_queued_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        std::deque<std::function<void()>> queue_;
        bool stopping_{false};
        std::vector<std::thread> workers_;
    };
} // namespace corvus::gateway