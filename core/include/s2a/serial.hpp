#pragma once

#include "s2a/result.hpp"

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

/// Serial port of a board, used by the Live mode (uploads go through arduino-cli).
namespace s2a::serial {

/// Line-based text connection. SerialPort implements it; tests use fakes.
class LineTransport {
public:
    LineTransport() = default;
    LineTransport(const LineTransport&) = delete;
    LineTransport& operator=(const LineTransport&) = delete;
    LineTransport(LineTransport&&) = delete;
    LineTransport& operator=(LineTransport&&) = delete;
    virtual ~LineTransport() = default;

    /// Sends `line` followed by '\n'.
    [[nodiscard]] virtual Status writeLine(std::string_view line) = 0;
    /// Next complete line (without "\r\n"), or nullopt when nothing arrived before `timeout`.
    [[nodiscard]] virtual Result<std::optional<std::string>> readLine(std::chrono::milliseconds timeout) = 0;
};

class SerialPort final : public LineTransport {
public:
    /// Opens `port` ("/dev/ttyACM0", "COM3") in 8N1 at `baud`. Opening resets most Arduino boards.
    [[nodiscard]] static Result<std::unique_ptr<SerialPort>> open(const std::string& port, int baud);
    ~SerialPort() override;

    [[nodiscard]] Status writeLine(std::string_view line) override;
    [[nodiscard]] Result<std::optional<std::string>> readLine(std::chrono::milliseconds timeout) override;

    struct Handle;

private:
    explicit SerialPort(std::unique_ptr<Handle> handle);

    std::unique_ptr<Handle> handle_;
    std::string buffer_; ///< Bytes received after the last complete line.
};

} // namespace s2a::serial
