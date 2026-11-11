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

TEST(SocketConnection, SpeaksTheLineProtocolWithABoard) {
    FakeBoard board;
    {
        auto opened = s2a::network::SocketConnection::openTcp("127.0.0.1", board.port(), milliseconds(2000));
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

TEST(SocketConnection, ReportsABoardThatCannotBeReached) {
    const auto opened = s2a::network::SocketConnection::openTcp("host.invalid", 3333, milliseconds(500));
    ASSERT_FALSE(opened.ok());
    EXPECT_EQ(opened.error().code, s2a::ErrorCode::NetworkFailed);
}

} // namespace
#endif

namespace {

using s2a::network::BluetoothDevice;

TEST(Bluetooth, ReadsTheAddressOfABluetoothPort) {
    EXPECT_EQ(s2a::network::bluetoothAddress("bt:24:6f:28:aa:bb:cc"), std::optional<std::string>("24:6F:28:AA:BB:CC"));
    EXPECT_FALSE(s2a::network::bluetoothAddress("bt:24:6F:28:AA:BB").has_value());
    EXPECT_FALSE(s2a::network::bluetoothAddress("bt:24-6F-28-AA-BB-CC").has_value());
    EXPECT_FALSE(s2a::network::bluetoothAddress("bt:24:6F:28:AA:BB:CG").has_value());
    EXPECT_FALSE(s2a::network::bluetoothAddress("COM3").has_value());
    EXPECT_FALSE(s2a::network::bluetoothAddress("wifi:192.168.4.1").has_value());
}

TEST(Bluetooth, KnowsTheWirelessPorts) {
    EXPECT_TRUE(s2a::network::isWireless("wifi:192.168.4.1"));
    EXPECT_TRUE(s2a::network::isWireless("bt:24:6F:28:AA:BB:CC"));
    EXPECT_TRUE(s2a::network::isWireless("/dev/cu.S2A-1A2B")); // paired on macOS
    EXPECT_TRUE(s2a::network::isWireless("/dev/cu.HC-05"));    // an HC-05 module paired on macOS
    EXPECT_TRUE(s2a::network::isWireless("/dev/cu.DSDTECHHC-06-SPPDev"));
    EXPECT_FALSE(s2a::network::isWireless("/dev/cu.usbmodem14101"));
    EXPECT_FALSE(s2a::network::isWireless("/dev/ttyUSB0"));
    EXPECT_FALSE(s2a::network::isWireless("COM3"));
}

TEST(Bluetooth, KnowsTheNamesOfTheModules) {
    for (const char* name : {"HC-05", "HC-06", "hc05", "DSD TECH HC-05", "linvor", "H-C-2010-06-01", "BT05", "JDY-31-SPP"}) {
        EXPECT_TRUE(s2a::network::isModuleName(name)) << name;
    }
    for (const char* name : {"S2A-1A2B", "Headphones", "HC", ""}) {
        EXPECT_FALSE(s2a::network::isModuleName(name)) << name;
    }
}

TEST(Bluetooth, KeepsTheBoardsListedByBluetoothctl) {
    const std::string output = "Device 24:6F:28:AA:BB:CC S2A-BBCC\n"
                               "Device 11:22:33:44:55:66 Headphones\n"
                               "Device 98:D3:31:F5:12:34 HC-05\n"
                               "\x1b[0;94m[bluetooth]\x1b[0m# Device a4:cf:12:01:02:03 S2A-0203 \r\n"
                               "Device 24:6F:28:AA:BB:CC S2A-BBCC\n"
                               "Device 77:88:99:AA:BB:CC\n"
                               "garbage\n";
    const std::vector<BluetoothDevice> expected{{"S2A-BBCC", "24:6F:28:AA:BB:CC"}, {"HC-05", "98:D3:31:F5:12:34"},
                                                {"S2A-0203", "A4:CF:12:01:02:03"}};
    EXPECT_EQ(s2a::network::parseBluetoothctlDevices(output), expected);
    EXPECT_TRUE(s2a::network::parseBluetoothctlDevices("").empty());
}

TEST(Bluetooth, ReportsABoardThatCannotBeReached) {
    const auto malformed = s2a::network::SocketConnection::openBluetooth("not an address", std::chrono::milliseconds(500));
    ASSERT_FALSE(malformed.ok());
    EXPECT_EQ(malformed.error().code, s2a::ErrorCode::NetworkFailed);
#ifndef _WIN32 // on Windows, connecting to a missing device can take several seconds
    const auto absent = s2a::network::SocketConnection::openBluetooth("00:00:00:00:00:01", std::chrono::milliseconds(500));
    ASSERT_FALSE(absent.ok());
    EXPECT_EQ(absent.error().code, s2a::ErrorCode::NetworkFailed);
#endif
}

} // namespace
