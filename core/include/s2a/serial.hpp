#pragma once

#include "s2a/result.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

/// Serial monitor: reads the lines a board prints (Serial.println) on a background thread and
/// sends the lines typed by the user. Owns the connection until destroyed.
class Monitor {
public:
    /// Speeds offered by the editor (the generated code uses 9600).
    static constexpr int kBauds[] = {9600, 19200, 57600, 115200};
    static constexpr std::size_t kMaxLines = 500;

    struct Snapshot {
        bool open = false;              ///< False once the connection failed (board unplugged...).
        std::vector<std::string> lines; ///< Last lines received (at most kMaxLines)...
        std::size_t count = 0;          ///< ...out of this many since the monitor was opened.
        std::optional<Error> error;
    };

    explicit Monitor(std::unique_ptr<LineTransport> transport);
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    Monitor(Monitor&&) = delete;
    Monitor& operator=(Monitor&&) = delete;
    ~Monitor();

    /// Opens a real serial port.
    [[nodiscard]] static Result<std::unique_ptr<Monitor>> open(const std::string& port, int baud);

    /// Queues `line` to be sent to the board (followed by '\n').
    void send(std::string line);
    [[nodiscard]] Snapshot snapshot() const;

private:
    void run();

    std::unique_ptr<LineTransport> transport_;
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::deque<std::string> outgoing_;
    std::atomic<bool> stopping_{false};
    std::thread thread_; ///< Started by the constructor once everything else is ready.
};

} // namespace s2a::serial
