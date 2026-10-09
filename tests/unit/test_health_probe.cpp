#include <gtest/gtest.h>
#include "corvus/gateway/health_probe.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using corvus::gateway::probe_http_health;

namespace
{
    class FakeServer
    {
    public:
        explicit FakeServer(std::string reply, bool hang = false)
            : reply_(std::move(reply)), hang_(hang)
        {
            fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            addr.sin_port = 0;
            ::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
            ::listen(fd_, 1);
            socklen_t len = sizeof addr;
            ::getsockname(fd_, reinterpret_cast<sockaddr *>(&addr), &len);
            port_ = ntohs(addr.sin_port);

            thread_ = std::thread([this]
                                  {
                const int c = ::accept(fd_, nullptr, nullptr);
                if (c < 0)
                    return;
                char buf[1024];
                const auto n = ::recv(c, buf, sizeof buf, 0);
                if (n > 0)
                    request_.assign(buf, static_cast<std::size_t>(n));
                if (hang_)
                {
                    while (!stop_)
                        std::this_thread::sleep_for(10ms);
                }
                else
                {
                    ::send(c, reply_.data(), reply_.size(), MSG_NOSIGNAL);
                }
                ::close(c); });
        }

        ~FakeServer()
        {
            stop_ = true;
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            if (thread_.joinable())
                thread_.join();
        }

        int port() const { return port_; }
        const std::string &request() const { return request_; }

    private:
        int fd_{-1};
        int port_{0};
        std::string reply_;
        bool hang_;
        std::string request_;
        std::atomic<bool> stop_{false};
        std::thread thread_;
    };

    int unused_port()
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr);
        socklen_t len = sizeof addr;
        ::getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len);
        ::close(fd);
        return ntohs(addr.sin_port);
    }
} // namespace

TEST(HealthProbeTest, HealthyOn200)
{
    FakeServer server("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
    const auto r = probe_http_health("127.0.0.1", server.port());
    EXPECT_TRUE(r.healthy) << r.detail;
}

TEST(HealthProbeTest, SendsGetForTheHealthPath)
{
    {
        FakeServer server("HTTP/1.1 200 OK\r\n\r\n");
        probe_http_health("127.0.0.1", server.port());
        EXPECT_EQ(server.request().rfind("GET /health HTTP/1.1\r\n", 0), 0u)
            << server.request();
    }
}

TEST(HealthProbeTest, UnhealthyOnNon200)
{
    FakeServer server("HTTP/1.1 503 Service Unavailable\r\n\r\n");
    const auto r = probe_http_health("127.0.0.1", server.port());
    EXPECT_FALSE(r.healthy);
    EXPECT_NE(r.detail.find("503"), std::string::npos) << r.detail;
}

TEST(HealthProbeTest, UnhealthyWhenNothingListens)
{
    const auto r = probe_http_health("127.0.0.1", unused_port());
    EXPECT_FALSE(r.healthy);
}

TEST(HealthProbeTest, UnhealthyOnGarbage)
{
    FakeServer server("not http at all\r\n");
    EXPECT_FALSE(probe_http_health("127.0.0.1", server.port()).healthy);
}

TEST(HealthProbeTest, HungServerFailsWithinTheDeadline)
{
    FakeServer server("", /*hang=*/true);

    const auto start = std::chrono::steady_clock::now();
    const auto r = probe_http_health("127.0.0.1", server.port(), "/health", 300ms);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(r.healthy);
    EXPECT_LT(elapsed, 1s);
}