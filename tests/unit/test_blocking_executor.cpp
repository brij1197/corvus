#include <gtest/gtest.h>
#include "corvus/gateway/blocking_executor.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using corvus::gateway::BlockingExecutor;
using namespace std::chrono_literals;

TEST(BlockingExecutorTest, RunsSubmittedTasks)
{
    BlockingExecutor ex(2, 16);
    std::promise<int> p;
    ASSERT_TRUE(ex.try_submit([&]
                              { p.set_value(42); }));
    EXPECT_EQ(p.get_future().get(), 42);
}

TEST(BlockingExecutorTest, TasksRunOffTheCallingThread)
{
    BlockingExecutor ex(1, 4);
    std::promise<std::thread::id> p;
    ex.try_submit([&]
                  { p.set_value(std::this_thread::get_id()); });
    EXPECT_NE(p.get_future().get(), std::this_thread::get_id());
}

TEST(BlockingExecutorTest, KnowsItsOwnWorkerThreads)
{
    BlockingExecutor ex(1, 4);
    BlockingExecutor other(1, 4);
    EXPECT_FALSE(ex.on_worker_thread());

    std::promise<std::pair<bool, bool>> p;
    ex.try_submit([&]
                  { p.set_value({ex.on_worker_thread(), other.on_worker_thread()}); });
    const auto [mine, theirs] = p.get_future().get();
    EXPECT_TRUE(mine);
    EXPECT_FALSE(theirs);
}

TEST(BlockingExecutorTest, RunsUpToThreadCountTasksAtOnce)
{
    constexpr int kThreads = 4;
    BlockingExecutor ex(kThreads, 64);
    std::atomic<int> running{0}, peak{0};
    std::atomic<int> done{0};

    for (int i = 0; i < 16; ++i)
        ex.try_submit([&]
                      {
            const int now = ++running;
            int seen = peak.load();
            while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
            std::this_thread::sleep_for(20ms);
            --running;
            ++done; });

    while (done < 16)
        std::this_thread::sleep_for(5ms);
    EXPECT_EQ(peak, kThreads);
}

TEST(BlockingExecutorTest, RejectsWhenQueueIsFull)
{
    BlockingExecutor ex(1, 2);
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::promise<void> started;

    ASSERT_TRUE(ex.try_submit([&, gate]
                              { started.set_value(); gate.wait(); }));
    started.get_future().wait(); // the worker is now busy

    EXPECT_TRUE(ex.try_submit([] {}));
    EXPECT_TRUE(ex.try_submit([] {}));
    EXPECT_FALSE(ex.try_submit([] {})) << "third queued task exceeds max_queued=2";
    EXPECT_EQ(ex.queued(), 2u);

    release.set_value();
}

TEST(BlockingExecutorTest, ThrowingTaskDoesNotKillTheWorker)
{
    BlockingExecutor ex(1, 4);
    ex.try_submit([]
                  { throw std::runtime_error("boom"); });
    std::promise<void> p;
    ex.try_submit([&]
                  { p.set_value(); });
    EXPECT_EQ(p.get_future().wait_for(2s), std::future_status::ready);
}

TEST(BlockingExecutorTest, DestructorRunsQueuedWorkThenJoins)
{
    std::atomic<int> ran{0};
    {
        BlockingExecutor ex(1, 16);
        for (int i = 0; i < 8; ++i)
            ex.try_submit([&]
                          { std::this_thread::sleep_for(2ms); ++ran; });
    }
    EXPECT_EQ(ran, 8);
}