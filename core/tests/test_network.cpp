#include "s2a/live.hpp"
#include "s2a/network.hpp"

#include <gtest/gtest.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

using std::chrono::milliseconds;

/// A board on this computer: listens on a free port, greets, answers "ok" to each line.
class FakeBoard {
public:
    FakeBoard() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        ::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        ::listen(listener_, 1);
        socklen_t size = sizeof(address);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &size);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { serve(); });
    }
    FakeBoard(const FakeBoard&) = delete;
    FakeBoard& operator=(const FakeBoard&) = delete;
    FakeBoard(FakeBoard&&) = delete;
    FakeBoard& operator=(FakeBoard&&) = delete;
    ~FakeBoard() {
        thread_.join();
        ::close(listener_);
    }

    [[nodiscard]] int port() const { return port_; }

private:
    void serve() {
        const int client = ::accept(listener_, nullptr, nullptr);
        const std::string hello = std::string(s2a::live::kHello) + "\r\n";
        ::send(client, hello.data(), hello.size(), 0);
        char chunk[64];
        for (;;) {
            const auto count = ::recv(client, chunk, sizeof(chunk), 0);
            if (count <= 0) {
                break;
            }
            for (auto c = chunk; c != chunk + count; ++c) {
                if (*c == '\n') {
                    ::send(client, "ok\n", 3, 0);
                }
            }
        }
        ::close(client);
    }

    int listener_ = -1;
    int port_ = 0;
    std::thread thread_;
};

TEST(TcpConnection, SpeaksTheLineProtocolWithABoard) {
    FakeBoard board;
    {
        auto opened = s2a::network::TcpConnection::open("127.0.0.1", board.port(), milliseconds(2000));
        ASSERT_TRUE(opened.ok()) << opened.error().detail;
        auto& connection = *opened.value();
        auto hello = connection.readLine(milliseconds(2000));
        ASSERT_TRUE(hello.ok());
        EXPECT_EQ(hello.value(), std::optional<std::string>(std::string(s2a::live::kHello))); // "\r" removed
        ASSERT_TRUE(connection.writeLine("D 2 1").ok());
        EXPECT_EQ(connection.readLine(milliseconds(2000)).value(), std::optional<std::string>("ok"));
        // Nothing more comes: no line before the timeout, no error.
        auto nothing = connection.readLine(milliseconds(50));
        ASSERT_TRUE(nothing.ok());
        EXPECT_FALSE(nothing.value().has_value());
        // The Live protocol works over it as over USB.
        s2a::live::LiveLink link(connection);
        link.digitalWrite(4, false);
        EXPECT_FALSE(link.takeError().has_value());
    }
}

TEST(TcpConnection, ReportsABoardThatCannotBeReached) {
    const auto opened = s2a::network::TcpConnection::open("host.invalid", 3333, milliseconds(500));
    ASSERT_FALSE(opened.ok());
    EXPECT_EQ(opened.error().code, s2a::ErrorCode::NetworkFailed);
}

} // namespace
#endif
