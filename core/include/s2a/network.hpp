#pragma once

#include "s2a/result.hpp"
#include "s2a/serial.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

/// Wi-Fi connection to a board running the S2A live firmware (ESP32): the same line protocol
/// as over USB, on a TCP connection.
namespace s2a::network {

/// TCP port of the S2A live firmware.
inline constexpr int kLivePort = 3333;
/// Address of the board on its own Wi-Fi network ("S2A-xxxx").
inline constexpr std::string_view kAccessPointAddress = "192.168.4.1";
/// Prefix of the "ports" that are Wi-Fi addresses: "wifi:192.168.4.1".
inline constexpr std::string_view kWifiPrefix = "wifi:";

/// The address of a "wifi:<address>" port, or nullopt for a serial port.
[[nodiscard]] std::optional<std::string> wifiAddress(std::string_view port);

class TcpConnection final : public serial::LineTransport {
public:
    /// Connects to `host` (name or IP address) on `port`, giving up after `timeout`.
    [[nodiscard]] static Result<std::unique_ptr<TcpConnection>> open(const std::string& host, int port,
                                                                      std::chrono::milliseconds timeout);
    ~TcpConnection() override;

    [[nodiscard]] Status writeLine(std::string_view line) override;
    [[nodiscard]] Result<std::optional<std::string>> readLine(std::chrono::milliseconds timeout) override;

    struct Socket;

private:
    explicit TcpConnection(std::unique_ptr<Socket> socket);

    std::unique_ptr<Socket> socket_;
    std::string buffer_; ///< Bytes received after the last complete line.
};

} // namespace s2a::network
