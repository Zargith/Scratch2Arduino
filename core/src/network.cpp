#include "s2a/network.hpp"

#include "s2a/process.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
// After winsock2.h: Bluetooth sockets (AF_BTH) and device search.
#include <ws2bth.h>
#include <bluetoothapis.h>
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

#if defined(__linux__)
// Linux Bluetooth sockets, declared here so that the BlueZ headers are not needed to build.
constexpr int kAfBluetooth = 31;   // AF_BLUETOOTH
constexpr int kBtProtoRfcomm = 3; // BTPROTO_RFCOMM
struct RfcommAddress {             // struct sockaddr_rc
    sa_family_t family;
    std::uint8_t address[6]; // bdaddr_t: last byte first
    std::uint8_t channel;
};
#endif

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

/// Connects `native` to `address` before `deadline`; on failure, closes it and explains why.
bool connectBefore(Native native, const sockaddr* address, int size, Clock::time_point deadline, std::string& failure) {
    if (!setNonBlocking(native)) {
        failure = describe(lastError());
        closeNative(native);
        return false;
    }
    bool connected = ::connect(native, address, size) == 0;
    if (!connected && inProgress(lastError())) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() > 0 && wait(native, true, left)) {
            int error = 0;
            socklen_t length = sizeof(error);
            getsockopt(native, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length);
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
    }
    return connected;
}

/// The 6 bytes of "24:6F:28:AA:BB:CC", first byte first.
std::array<std::uint8_t, 6> addressBytes(const std::string& address) {
    std::array<std::uint8_t, 6> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(std::stoul(address.substr(i * 3, 2), nullptr, 16));
    }
    return bytes;
}

bool isBoardName(std::string_view name) {
    return name.substr(0, kBoardNamePrefix.size()) == kBoardNamePrefix || isModuleName(name);
}

#ifdef _WIN32
std::string toUtf8(const wchar_t* text) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::string formatAddress(ULONGLONG address) {
    std::string text;
    for (int shift = 40; shift >= 0; shift -= 8) {
        static constexpr std::string_view kDigits = "0123456789ABCDEF";
        const auto byte = static_cast<unsigned>((address >> shift) & 0xFF);
        text += kDigits[byte >> 4];
        text += kDigits[byte & 0xF];
        if (shift > 0) {
            text += ':';
        }
    }
    return text;
}
#endif

} // namespace

struct SocketConnection::Socket {
    Native native = kInvalid;
    std::string host;
};

std::optional<std::string> wifiAddress(std::string_view port) {
    if (port.substr(0, kWifiPrefix.size()) != kWifiPrefix || port.size() == kWifiPrefix.size()) {
        return std::nullopt;
    }
    return std::string(port.substr(kWifiPrefix.size()));
}

std::optional<std::string> bluetoothAddress(std::string_view port) {
    if (port.substr(0, kBluetoothPrefix.size()) != kBluetoothPrefix) {
        return std::nullopt;
    }
    std::string address(port.substr(kBluetoothPrefix.size()));
    if (address.size() != 17) {
        return std::nullopt;
    }
    for (std::size_t i = 0; i < address.size(); ++i) {
        const auto c = static_cast<unsigned char>(address[i]);
        if (i % 3 == 2 ? c != ':' : std::isxdigit(c) == 0) {
            return std::nullopt;
        }
        address[i] = static_cast<char>(std::toupper(c));
    }
    return address;
}

bool isModuleName(std::string_view name) {
    std::string upper(name);
    for (char& c : upper) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    // Names given by the makers ("H-C-2010-06-01" and "linvor" on older HC-06, "BT05", "JDY-31-SPP"...).
    constexpr std::array<std::string_view, 8> kNames{"HC-05", "HC-06", "HC05", "HC06", "H-C-2010", "LINVOR", "BT05", "JDY-3"};
    return std::any_of(kNames.begin(), kNames.end(),
                       [&upper](std::string_view known) { return upper.find(known) != std::string::npos; });
}

bool isWireless(std::string_view port) {
    // A board or module paired on macOS is a serial port named after it: /dev/cu.S2A-1A2B, /dev/cu.HC-05.
    const auto slash = port.rfind('/');
    return wifiAddress(port).has_value() || bluetoothAddress(port).has_value() ||
           port.find(kBoardNamePrefix) != std::string_view::npos ||
           (slash != std::string_view::npos && isModuleName(port.substr(slash + 1)));
}

bool bluetoothByAddress() noexcept {
#if defined(__linux__) || defined(_WIN32)
    return true;
#else
    return false;
#endif
}

std::vector<BluetoothDevice> parseBluetoothctlDevices(std::string_view output) {
    std::vector<BluetoothDevice> devices;
    constexpr std::string_view kDevice = "Device ";
    std::size_t start = 0;
    while (start < output.size()) {
        auto end = output.find('\n', start);
        if (end == std::string_view::npos) {
            end = output.size();
        }
        std::string_view line = output.substr(start, end - start);
        start = end + 1;
        const auto found = line.find(kDevice);
        if (found == std::string_view::npos) {
            continue;
        }
        line.remove_prefix(found + kDevice.size());
        const auto address = bluetoothAddress(std::string(kBluetoothPrefix) + std::string(line.substr(0, 17)));
        if (!address || line.size() < 19) {
            continue;
        }
        std::string name(line.substr(18));
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())) != 0) {
            name.pop_back();
        }
        BluetoothDevice device{name, *address};
        if (isBoardName(name) && std::find(devices.begin(), devices.end(), device) == devices.end()) {
            devices.push_back(std::move(device));
        }
    }
    return devices;
}

Result<std::vector<BluetoothDevice>> findBluetoothBoards(std::chrono::seconds duration) {
    const auto unavailable = [](const std::string& detail) { return networkError("Bluetooth", detail); };
#if defined(_WIN32)
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnUnknown = TRUE;
    search.fReturnConnected = TRUE;
    search.fIssueInquiry = TRUE;
    // In steps of 1.28 s.
    search.cTimeoutMultiplier = static_cast<UCHAR>(std::clamp<long long>(duration.count() * 100 / 128, 1, 48));
    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);
    const HBLUETOOTH_DEVICE_FIND find = BluetoothFindFirstDevice(&search, &info);
    std::vector<BluetoothDevice> devices;
    if (find == nullptr) {
        const DWORD error = GetLastError();
        if (error == ERROR_NO_MORE_ITEMS) {
            return devices;
        }
        return unavailable("Bluetooth is off or missing (error " + std::to_string(error) + ")");
    }
    do {
        BluetoothDevice device{toUtf8(info.szName), formatAddress(info.Address.ullLong)};
        if (isBoardName(device.name) && std::find(devices.begin(), devices.end(), device) == devices.end()) {
            devices.push_back(std::move(device));
        }
        info = BLUETOOTH_DEVICE_INFO{};
        info.dwSize = sizeof(info);
    } while (BluetoothFindNextDevice(find, &info) != FALSE);
    BluetoothFindDeviceClose(find);
    return devices;
#elif defined(__linux__)
    const auto bluetoothctl = process::findInPath("bluetoothctl");
    if (bluetoothctl.empty()) {
        return unavailable("bluetoothctl is missing (install the bluez package)");
    }
    const auto scanned = process::run({bluetoothctl,
                                       {"--timeout", std::to_string(duration.count()), "scan", "on"},
                                       {},
                                       {},
                                       duration + std::chrono::seconds(10)});
    if (!scanned.ok()) {
        return unavailable(scanned.error().detail);
    }
    const std::string scan = scanned.value().out + scanned.value().err;
    for (const char* off : {"No default controller", "NotReady", "org.bluez.Error.Failed"}) {
        if (scan.find(off) != std::string::npos) {
            return unavailable("Bluetooth is off or missing: " + scan.substr(0, 200));
        }
    }
    const auto listed = process::run({bluetoothctl, {"devices"}, {}, {}, std::chrono::seconds(10)});
    if (!listed.ok()) {
        return unavailable(listed.error().detail);
    }
    return parseBluetoothctlDevices(listed.value().out);
#else
    (void)duration;
    (void)unavailable;
    return std::vector<BluetoothDevice>{};
#endif
}

Result<std::unique_ptr<SocketConnection>> SocketConnection::openBluetooth(const std::string& address,
                                                                          std::chrono::milliseconds timeout) {
    const auto checked = bluetoothAddress(std::string(kBluetoothPrefix) + address);
    if (!checked) {
        return networkError(address, "not a Bluetooth address");
    }
    if (!startNetworking()) {
        return networkError(address, "Bluetooth cannot be used");
    }
    const auto bytes = addressBytes(*checked);
    std::string failure = "Bluetooth connections are not supported on this computer";
#if defined(__linux__)
    // The serial service of the ESP32 is on channel 1, or the next ones when that one is taken.
    const auto deadline = Clock::now() + timeout;
    for (std::uint8_t channel = 1; channel <= 3 && Clock::now() < deadline; ++channel) {
        const Native native = ::socket(kAfBluetooth, SOCK_STREAM, kBtProtoRfcomm);
        if (native == kInvalid) {
            failure = "Bluetooth is off or missing (" + describe(lastError()) + ")";
            break;
        }
        RfcommAddress target{};
        target.family = kAfBluetooth;
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            target.address[i] = bytes[bytes.size() - 1 - i];
        }
        target.channel = channel;
        if (connectBefore(native, reinterpret_cast<const sockaddr*>(&target), sizeof(target), deadline, failure)) {
            auto socket = std::make_unique<Socket>();
            socket->native = native;
            socket->host = *checked;
            return std::unique_ptr<SocketConnection>(new SocketConnection(std::move(socket)));
        }
    }
#elif defined(_WIN32)
    (void)timeout; // Windows looks the serial service up itself and has its own time limit
    const Native native = ::socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (native == kInvalid) {
        return networkError(*checked, "Bluetooth is off or missing (" + describe(lastError()) + ")");
    }
    SOCKADDR_BTH target{};
    target.addressFamily = AF_BTH;
    for (const auto byte : bytes) {
        target.btAddr = (target.btAddr << 8) | byte;
    }
    // Serial Port Profile: 00001101-0000-1000-8000-00805F9B34FB.
    target.serviceClassId = GUID{0x00001101, 0x0000, 0x1000, {0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB}};
    target.port = 0;
    if (::connect(native, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == 0 && setNonBlocking(native)) {
        auto socket = std::make_unique<Socket>();
        socket->native = native;
        socket->host = *checked;
        return std::unique_ptr<SocketConnection>(new SocketConnection(std::move(socket)));
    }
    failure = describe(lastError());
    closeNative(native);
#else
    (void)timeout;
    (void)bytes;
#endif
    return networkError(*checked, failure);
}

Result<std::unique_ptr<SocketConnection>> SocketConnection::openTcp(const std::string& host, int port,
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
        if (!connectBefore(native, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen), deadline, failure)) {
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
        return std::unique_ptr<SocketConnection>(new SocketConnection(std::move(socket)));
    }
    freeaddrinfo(found);
    return networkError(host, failure);
}

SocketConnection::SocketConnection(std::unique_ptr<Socket> socket) : socket_(std::move(socket)) {}

SocketConnection::~SocketConnection() {
    if (socket_ && socket_->native != kInvalid) {
        closeNative(socket_->native);
    }
}

Status SocketConnection::writeLine(std::string_view line) {
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

Result<std::optional<std::string>> SocketConnection::readLine(std::chrono::milliseconds timeout) {
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
