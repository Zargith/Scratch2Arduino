#include "s2a/network.hpp"

#include <array>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace s2a::network {

namespace {

using Clock = std::chrono::steady_clock;

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kInvalid = INVALID_SOCKET;

int lastError() { return WSAGetLastError(); }
void closeNative(Native socket) { closesocket(socket); }
bool inProgress(int error) { return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS; }

/// Winsock must be started once per process before any socket call.
bool startNetworking() {
    static const bool started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return started;
}

bool setNonBlocking(Native socket) {
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
}

std::string describe(int error) { return "socket error " + std::to_string(error); }
#else
using Native = int;
constexpr Native kInvalid = -1;

int lastError() { return errno; }
void closeNative(Native socket) { ::close(socket); }
bool inProgress(int error) { return error == EINPROGRESS || error == EAGAIN || error == EWOULDBLOCK; }
bool startNetworking() { return true; }

bool setNonBlocking(Native socket) {
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}

std::string describe(int error) { return std::strerror(error); } // NOLINT(concurrency-mt-unsafe)
#endif

Error networkError(const std::string& host, const std::string& detail) {
    return Error{ErrorCode::NetworkFailed, host + ": " + detail};
}

/// Waits until `socket` can be read (or written); false when `timeout` passed first.
bool wait(Native socket, bool forWriting, std::chrono::milliseconds timeout) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval limit{};
    limit.tv_sec = static_cast<long>(timeout.count() / 1000);
    limit.tv_usec = static_cast<long>((timeout.count() % 1000) * 1000);
    const int status = ::select(static_cast<int>(socket + 1), forWriting ? nullptr : &set,
                                forWriting ? &set : nullptr, nullptr, &limit);
    return status > 0;
}

/// Takes the first complete line out of `buffer`.
std::optional<std::string> takeLine(std::string& buffer) {
    const auto end = buffer.find('\n');
    if (end == std::string::npos) {
        return std::nullopt;
    }
    std::string line = buffer.substr(0, end);
    buffer.erase(0, end + 1);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

} // namespace

struct TcpConnection::Socket {
    Native native = kInvalid;
    std::string host;
};

std::optional<std::string> wifiAddress(std::string_view port) {
    if (port.substr(0, kWifiPrefix.size()) != kWifiPrefix || port.size() == kWifiPrefix.size()) {
        return std::nullopt;
    }
    return std::string(port.substr(kWifiPrefix.size()));
}

Result<std::unique_ptr<TcpConnection>> TcpConnection::open(const std::string& host, int port,
                                                           std::chrono::milliseconds timeout) {
    if (!startNetworking()) {
        return networkError(host, "the network cannot be used");
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (const int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found);
        status != 0 || found == nullptr) {
        return networkError(host, "address not found (" + std::string(gai_strerror(status)) + ")");
    }
    const auto deadline = Clock::now() + timeout;
    std::string failure = "no answer";
    for (const addrinfo* candidate = found; candidate != nullptr; candidate = candidate->ai_next) {
        const Native native = ::socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (native == kInvalid) {
            failure = describe(lastError());
            continue;
        }
        if (!setNonBlocking(native)) {
            failure = describe(lastError());
            closeNative(native);
            continue;
        }
        bool connected = ::connect(native, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0;
        if (!connected && inProgress(lastError())) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            if (left.count() > 0 && wait(native, true, left)) {
                int error = 0;
                socklen_t size = sizeof(error);
                getsockopt(native, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size);
                connected = error == 0;
                if (!connected) {
                    failure = describe(error);
                }
            } else {
                failure = "no answer";
            }
        } else if (!connected) {
            failure = describe(lastError());
        }
        if (!connected) {
            closeNative(native);
            continue;
        }
        int enabled = 1; // short command lines: send them at once
        setsockopt(native, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#ifdef SO_NOSIGPIPE
        setsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
        freeaddrinfo(found);
        auto socket = std::make_unique<Socket>();
        socket->native = native;
        socket->host = host;
        return std::unique_ptr<TcpConnection>(new TcpConnection(std::move(socket)));
    }
    freeaddrinfo(found);
    return networkError(host, failure);
}

TcpConnection::TcpConnection(std::unique_ptr<Socket> socket) : socket_(std::move(socket)) {}

TcpConnection::~TcpConnection() {
    if (socket_ && socket_->native != kInvalid) {
        closeNative(socket_->native);
    }
}

Status TcpConnection::writeLine(std::string_view line) {
    std::string data(line);
    data.push_back('\n');
    std::size_t sent = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(2000);
#ifdef MSG_NOSIGNAL
    constexpr int kFlags = MSG_NOSIGNAL; // a closed connection is an error, not a signal
#else
    constexpr int kFlags = 0;
#endif
    while (sent < data.size()) {
        const auto count = ::send(socket_->native, data.data() + sent, static_cast<int>(data.size() - sent), kFlags);
        if (count > 0) {
            sent += static_cast<std::size_t>(count);
            continue;
        }
        const int error = lastError();
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (!inProgress(error) || left.count() <= 0 || !wait(socket_->native, true, left)) {
            return networkError(socket_->host, "the connection was lost (" + describe(error) + ")");
        }
    }
    return success();
}

Result<std::optional<std::string>> TcpConnection::readLine(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    for (;;) {
        if (auto line = takeLine(buffer_)) {
            return line;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0 || !wait(socket_->native, false, left)) {
            return std::optional<std::string>();
        }
        std::array<char, 256> chunk{};
        const auto received = ::recv(socket_->native, chunk.data(), static_cast<int>(chunk.size()), 0);
        if (received > 0) {
            buffer_.append(chunk.data(), static_cast<std::size_t>(received));
        } else if (received == 0) {
            return networkError(socket_->host, "the board closed the connection");
        } else if (!inProgress(lastError())) {
            return networkError(socket_->host, "the connection was lost (" + describe(lastError()) + ")");
        }
    }
}

} // namespace s2a::network
