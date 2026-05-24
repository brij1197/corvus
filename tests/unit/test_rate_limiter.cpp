#include <gtest/gtest.h>
#include "corvus/gateway/rate_limiter.h"
#include <thread>

using namespace corvus::gateway;

TEST(RateLimiterTest, AllowsRequestsUpToLimit)
{
    RateLimitConfig cfg{.max_tokens = 5, .refill_rate = 0.0};
    RateLimiter limiter(cfg);

    for (int i = 0; i < 5; ++i)
    {
        EXPECT_TRUE(limiter.allow("client-a"))
            << "Request " << i << " should be allowed";
    }

    EXPECT_FALSE(limiter.allow("client-a"))
        << "6th request should be denied";
}

TEST(RateLimiterTest, DifferentClientsAreIndependent)
{
    RateLimitConfig cfg{.max_tokens = 2, .refill_rate = 0.0};
    RateLimiter limiter(cfg);

    EXPECT_TRUE(limiter.allow("alice"));
    EXPECT_TRUE(limiter.allow("alice"));
    EXPECT_FALSE(limiter.allow("alice"));

    EXPECT_TRUE(limiter.allow("bob"));
    EXPECT_TRUE(limiter.allow("bob"));
    EXPECT_FALSE(limiter.allow("bob"));
}

TEST(RateLimiterTest, TokensRefillOverTime)
{
    RateLimitConfig cfg{.max_tokens = 2, .refill_rate = 100.0};
    RateLimiter limiter(cfg);

    EXPECT_TRUE(limiter.allow("client"));
    EXPECT_TRUE(limiter.allow("client"));
    EXPECT_FALSE(limiter.allow("client"));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_TRUE(limiter.allow("client"))
        << "Token should have refilled after 50ms at 100 tokens/sec";
}

TEST(RateLimiterTest, TokensDoNotExceedMax)
{
    RateLimitConfig cfg{.max_tokens = 3, .refill_rate = 1000.0};
    RateLimiter limiter(cfg);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(limiter.allow("client"));
    EXPECT_TRUE(limiter.allow("client"));
    EXPECT_TRUE(limiter.allow("client"));
    EXPECT_FALSE(limiter.allow("client"))
        << "Should not exceed max_tokens even after long refill period";
}

TEST(RateLimiterTest, RemainingReturnsMaxForUnknownClient)
{
    RateLimitConfig cfg{.max_tokens = 10, .refill_rate = 1.0};
    RateLimiter limiter(cfg);

    EXPECT_EQ(limiter.remaining("never-seen"), 10u);
}

TEST(RateLimiterTest, RemainingDecrementsAfterAllow)
{
    RateLimitConfig cfg{.max_tokens = 5, .refill_rate = 0.0};
    RateLimiter limiter(cfg);

    limiter.allow("client");
    EXPECT_EQ(limiter.remaining("client"), 4u);

    limiter.allow("client");
    limiter.allow("client");
    EXPECT_EQ(limiter.remaining("client"), 2u);
}

TEST(RateLimiterTest, RetryAfterIsZeroWhenNotLimited)
{
    RateLimitConfig cfg{.max_tokens = 10, .refill_rate = 1.0};
    RateLimiter limiter(cfg);

    EXPECT_DOUBLE_EQ(limiter.retry_after("new-client"), 0.0);

    limiter.allow("new-client");
    EXPECT_DOUBLE_EQ(limiter.retry_after("new-client"), 0.0);
}

TEST(RateLimiterTest, RetryAfterIsPositiveWhenExhausted)
{
    RateLimitConfig cfg{.max_tokens = 1, .refill_rate = 1.0};
    RateLimiter limiter(cfg);

    limiter.allow("client");
    EXPECT_FALSE(limiter.allow("client"));

    EXPECT_GT(limiter.retry_after("client"), 0.0);
}

TEST(RateLimiterTest, ClientCountTracksUniqueClients)
{
    RateLimitConfig cfg{.max_tokens = 10, .refill_rate = 1.0};
    RateLimiter limiter(cfg);

    EXPECT_EQ(limiter.client_count(), 0u);

    limiter.allow("a");
    EXPECT_EQ(limiter.client_count(), 1u);

    limiter.allow("b");
    limiter.allow("a"); // duplicate
    EXPECT_EQ(limiter.client_count(), 2u);
}

TEST(RateLimiterTest, StaleEntriesAreEvicted)
{
    RateLimitConfig cfg{
        .max_tokens = 10,
        .refill_rate = 1.0,
        .cleanup_interval = std::chrono::seconds(0),
        .entry_ttl = std::chrono::seconds(0),
    };
    RateLimiter limiter(cfg);

    limiter.allow("ephemeral");
    EXPECT_EQ(limiter.client_count(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    limiter.allow("other");
    EXPECT_EQ(limiter.client_count(), 1u)
        << "Stale entry should have been evicted during cleanup";
}

TEST(RateLimiterTest, DefaultConfigValues)
{
    RateLimiter limiter;
    const auto &cfg = limiter.config();

    EXPECT_EQ(cfg.max_tokens, 100u);
    EXPECT_DOUBLE_EQ(cfg.refill_rate, 10.0);
    EXPECT_EQ(cfg.cleanup_interval, std::chrono::seconds(300));
    EXPECT_EQ(cfg.entry_ttl, std::chrono::seconds(600));
}

TEST(RateLimiterTest, ConcurrentAccessDoesNotCrash)
{
    RateLimitConfig cfg{.max_tokens = 1000, .refill_rate = 100.0};
    auto limiter = std::make_shared<RateLimiter>(cfg);

    constexpr int num_threads = 8;
    constexpr int requests_per_thread = 500;

    std::vector<std::thread> threads;
    threads.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([limiter, t]()
                             {
            const std::string key = "thread-" + std::to_string(t);
            for (int i = 0; i < requests_per_thread; ++i)
            {
                limiter->allow(key);
                (void)limiter->remaining(key);
                (void)limiter->retry_after(key);
            } });
    }

    for (auto &th : threads)
    {
        th.join();
    }

    EXPECT_EQ(limiter->client_count(), static_cast<std::size_t>(num_threads));
}