#pragma once

#include "s2a/result.hpp"
#include "s2a/serial.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Wireless connections to a board running the S2A live firmware (ESP32): the same line
/// protocol as over USB, on a TCP connection (Wi-Fi) or an RFCOMM one (Bluetooth).
namespace s2a::network {

/// TCP port of the S2A live firmware.
inline constexpr int kLivePort = 3333;
/// Address of the board on its own Wi-Fi network ("S2A-xxxx").
inline constexpr std::string_view kAccessPointAddress = "192.168.4.1";
/// Prefix of the "ports" that are Wi-Fi addresses: "wifi:192.168.4.1".
inline constexpr std::string_view kWifiPrefix = "wifi:";

/// Prefix of the "ports" that are Bluetooth addresses: "bt:24:6F:28:AA:BB:CC".
inline constexpr std::string_view kBluetoothPrefix = "bt:";
/// Bluetooth (and Wi-Fi access point) name of a board running the S2A live firmware: "S2A-xxxx".
inline constexpr std::string_view kBoardNamePrefix = "S2A-";

/// The address of a "wifi:<address>" port, or nullopt for a serial port.
[[nodiscard]] std::optional<std::string> wifiAddress(std::string_view port);

/// The address ("24:6F:28:AA:BB:CC", upper case) of a "bt:<address>" port, or nullopt when the
/// port is not one or its address is malformed.
[[nodiscard]] std::optional<std::string> bluetoothAddress(std::string_view port);

/// A Wi-Fi or Bluetooth port ("wifi:", "bt:", or the serial port of a paired board such as
/// /dev/cu.S2A-1A2B on macOS): programs cannot be uploaded through it.
[[nodiscard]] bool isWireless(std::string_view port);

struct BluetoothDevice {
    std::string name;    ///< "S2A-1A2B"
    std::string address; ///< "24:6F:28:AA:BB:CC"

    friend bool operator==(const BluetoothDevice& a, const BluetoothDevice& b) {
        return a.name == b.name && a.address == b.address;
    }
};

/// Whether "bt:<address>" ports can be opened on this computer (Linux, Windows). On macOS, the
/// board is paired in the system settings and then appears as a serial port (/dev/cu.S2A-xxxx).
[[nodiscard]] bool bluetoothByAddress() noexcept;

/// The boards ("S2A-..." names) of the output of `bluetoothctl devices`.
[[nodiscard]] std::vector<BluetoothDevice> parseBluetoothctlDevices(std::string_view output);

/// Looks for the boards around the computer for about `duration` (and the ones already paired).
/// Fails with NetworkFailed when Bluetooth is off or missing.
[[nodiscard]] Result<std::vector<BluetoothDevice>> findBluetoothBoards(std::chrono::seconds duration);

/// A line connection over a socket: TCP (Wi-Fi) or RFCOMM (Bluetooth).
class SocketConnection final : public serial::LineTransport {
public:
    /// Connects to `host` (name or IP address) on `port`, giving up after `timeout`.
    [[nodiscard]] static Result<std::unique_ptr<SocketConnection>> openTcp(const std::string& host, int port,
                                                                           std::chrono::milliseconds timeout);
    /// Connects to the serial service (SPP) of the Bluetooth device at `address`.
    [[nodiscard]] static Result<std::unique_ptr<SocketConnection>> openBluetooth(const std::string& address,
                                                                                 std::chrono::milliseconds timeout);
    ~SocketConnection() override;
    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;
    SocketConnection(SocketConnection&&) = delete;
    SocketConnection& operator=(SocketConnection&&) = delete;

    [[nodiscard]] Status writeLine(std::string_view line) override;
    [[nodiscard]] Result<std::optional<std::string>> readLine(std::chrono::milliseconds timeout) override;

    struct Socket;

private:
    explicit SocketConnection(std::unique_ptr<Socket> socket);

    std::unique_ptr<Socket> socket_;
    std::string buffer_; ///< Bytes received after the last complete line.
};

} // namespace s2a::network
