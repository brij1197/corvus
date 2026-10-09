#include "corvus/gateway/health_probe.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace corvus::gateway
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        class Socket
        {
        public:
            Socket() : fd_(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {}
            ~Socket()
            {
                if (fd_ >= 0)
                    ::close(fd_);
            }
            Socket(const Socket &) = delete;
            Socket &operator=(const Socket &) = delete;
            int fd() const { return fd_; }

        private:
            int fd_;
        };

        int remaining_ms(Clock::time_point deadline)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now());
            return left.count() > 0 ? static_cast<int>(left.count()) : 0;
        }

        bool wait_for(int fd, short events, Clock::time_point deadline)
        {
            for (;;)
            {
                const int ms = remaining_ms(deadline);
                if (ms == 0)
                    return false;
                pollfd p{fd, events, 0};
                const int rc = ::poll(&p, 1, ms);
                if (rc > 0)
                    return (p.revents & (events | POLLHUP)) != 0;
                if (rc == 0)
                    return false;
                if (errno != EINTR)
                    return false;
            }
        }

        HealthProbeResult fail(std::string why) { return {false, std::move(why)}; }

    } // namespace

    HealthProbeResult probe_http_health(const std::string &host,
                                        int port,
                                        const std::string &path,
                                        std::chrono::milliseconds timeout)
    {
        const auto deadline = Clock::now() + timeout;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
            return fail("invalid IPv4 address: " + host);

        Socket sock;
        if (sock.fd() < 0)
            return fail(std::string("socket: ") + std::strerror(errno));

        ::fcntl(sock.fd(), F_SETFL, ::fcntl(sock.fd(), F_GETFL, 0) | O_NONBLOCK);
        if (::connect(sock.fd(), reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
        {
            if (errno != EINPROGRESS)
                return fail(std::string("connect: ") + std::strerror(errno));
            if (!wait_for(sock.fd(), POLLOUT, deadline))
                return fail("connect timed out");
            int err = 0;
            socklen_t len = sizeof err;
            ::getsockopt(sock.fd(), SOL_SOCKET, SO_ERROR, &err, &len);
            if (err != 0)
                return fail(std::string("connect: ") + std::strerror(err));
        }

        const std::string request = "GET " + path + " HTTP/1.1\r\n"
                                                    "Host: " +
                                    host + "\r\n"
                                           "User-Agent: corvus-health-check\r\n"
                                           "Connection: close\r\n\r\n";
        std::size_t sent = 0;
        while (sent < request.size())
        {
            if (!wait_for(sock.fd(), POLLOUT, deadline))
                return fail("send timed out");
            const auto n = ::send(sock.fd(), request.data() + sent,
                                  request.size() - sent, MSG_NOSIGNAL);
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                return fail(std::string("send: ") + std::strerror(errno));
            }
            sent += static_cast<std::size_t>(n);
        }

        std::string head;
        char buf[256];
        while (head.find("\r\n") == std::string::npos && head.size() < 1024)
        {
            if (!wait_for(sock.fd(), POLLIN, deadline))
                return fail("no response before deadline");
            const auto n = ::recv(sock.fd(), buf, sizeof buf, 0);
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                return fail(std::string("recv: ") + std::strerror(errno));
            }
            if (n == 0)
                break;
            head.append(buf, static_cast<std::size_t>(n));
        }

        const auto eol = head.find("\r\n");
        const std::string status_line = head.substr(0, eol);
        if (status_line.rfind("HTTP/1.", 0) != 0 || status_line.size() < 12)
            return fail("malformed response: '" + status_line + "'");

        const bool ok = status_line.compare(9, 3, "200") == 0;
        return {ok, status_line};
    }

} // namespace corvus::gateway