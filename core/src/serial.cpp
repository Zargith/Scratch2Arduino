#include "s2a/serial.hpp"

#include <array>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <filesystem>
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace s2a::serial {

namespace {

using Clock = std::chrono::steady_clock;

Error portError(const std::string& port, const std::string& detail) {
    return Error{ErrorCode::PortFailed, port + ": " + detail};
}

} // namespace

void LineBuffer::append(std::string_view bytes) {
    if (start_ > 0 && start_ >= data_.size() / 2) {
        data_.erase(0, start_); // taken lines are dropped in one go, not one by one
        start_ = 0;
    }
    data_.append(bytes);
}

std::optional<std::string> LineBuffer::take() {
    const auto end = data_.find('\n', start_);
    if (end == std::string::npos) {
        if (data_.size() - start_ < kMaxLine) {
            return std::nullopt;
        }
        // A "\r" at the cut stays for the next piece: "\r\n" split here gives no extra empty line.
        const std::size_t size = data_[start_ + kMaxLine - 1] == '\r' ? kMaxLine - 1 : kMaxLine;
        std::string piece = data_.substr(start_, size);
        start_ += size;
        return piece;
    }
    std::string line = data_.substr(start_, end - start_);
    start_ = end + 1;
    if (start_ == data_.size()) {
        data_.clear();
        start_ = 0;
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

bool isSupportedBaud(int baud) noexcept {
    switch (baud) {
    case 300:
    case 1200:
    case 2400:
    case 4800:
    case 9600:
    case 19200:
    case 38400:
    case 57600:
    case 115200:
    case 230400:
        return true;
    default:
        return false;
    }
}

#ifdef _WIN32

struct SerialPort::Handle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::string name;
};

Result<std::unique_ptr<SerialPort>> SerialPort::open(const std::string& port, int baud) {
    if (!isSupportedBaud(baud)) {
        return portError(port, std::to_string(baud) + " bauds is not a standard speed");
    }
    // "\\.\COM10": COM ports above 9 need the device namespace.
    const std::wstring path = L"\\\\.\\" + std::filesystem::u8path(port).wstring();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return portError(port, "CreateFile failed, error " + std::to_string(GetLastError()));
    }
    DCB settings{};
    settings.DCBlength = sizeof(settings);
    if (GetCommState(handle, &settings) == 0) {
        CloseHandle(handle);
        return portError(port, "GetCommState failed");
    }
    settings.BaudRate = static_cast<DWORD>(baud);
    settings.ByteSize = 8;
    settings.Parity = NOPARITY;
    settings.StopBits = ONESTOPBIT;
    settings.fBinary = TRUE;
    settings.fDtrControl = DTR_CONTROL_ENABLE;
    settings.fRtsControl = RTS_CONTROL_ENABLE;
    settings.fOutxCtsFlow = FALSE;
    settings.fOutxDsrFlow = FALSE;
    settings.fOutX = FALSE;
    settings.fInX = FALSE;
    if (SetCommState(handle, &settings) == 0) {
        CloseHandle(handle);
        return portError(port, "SetCommState failed");
    }
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD; // ReadFile returns at once with what is there
    timeouts.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(handle, &timeouts);
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR); // nothing left over from a previous program
    auto data = std::make_unique<Handle>();
    data->handle = handle;
    data->name = port;
    return std::unique_ptr<SerialPort>(new SerialPort(std::move(data)));
}

SerialPort::~SerialPort() {
    if (handle_ && handle_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_->handle);
    }
}

Status SerialPort::writeLine(std::string_view line) {
    const std::string data = std::string(line) + "\n";
    DWORD written = 0;
    if (WriteFile(handle_->handle, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) == 0 ||
        written != data.size()) {
        return portError(handle_->name, "write failed, error " + std::to_string(GetLastError()));
    }
    return success();
}

Result<std::optional<std::string>> SerialPort::readLine(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    for (;;) {
        if (auto line = buffer_.take()) {
            return line;
        }
        std::array<char, 256> chunk{};
        DWORD received = 0;
        if (ReadFile(handle_->handle, chunk.data(), static_cast<DWORD>(chunk.size()), &received, nullptr) == 0) {
            return portError(handle_->name, "read failed, error " + std::to_string(GetLastError()));
        }
        if (received > 0) {
            buffer_.append(std::string_view(chunk.data(), received));
            continue;
        }
        if (Clock::now() >= deadline) {
            return std::optional<std::string>();
        }
        Sleep(2);
    }
}

#else

struct SerialPort::Handle {
    int fd = -1;
    std::string name;
};

namespace {

/// Only called with a speed accepted by isSupportedBaud().
speed_t speedOf(int baud) {
    switch (baud) {
    case 300:
        return B300;
    case 1200:
        return B1200;
    case 2400:
        return B2400;
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 230400:
        return B230400;
    default:
        return B115200;
    }
}

} // namespace

Result<std::unique_ptr<SerialPort>> SerialPort::open(const std::string& port, int baud) {
    if (!isSupportedBaud(baud)) {
        return portError(port, std::to_string(baud) + " bauds is not a standard speed");
    }
    const int fd = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return portError(port, std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
    }
    termios settings{};
    if (tcgetattr(fd, &settings) != 0) {
        ::close(fd);
        return portError(port, "not a serial port");
    }
    cfmakeraw(&settings);
    cfsetispeed(&settings, speedOf(baud));
    cfsetospeed(&settings, speedOf(baud));
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~static_cast<tcflag_t>(CRTSCTS);
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &settings) != 0) {
        ::close(fd);
        return portError(port, "cannot configure the port");
    }
    tcflush(fd, TCIOFLUSH);
    auto data = std::make_unique<Handle>();
    data->fd = fd;
    data->name = port;
    return std::unique_ptr<SerialPort>(new SerialPort(std::move(data)));
}

SerialPort::~SerialPort() {
    if (handle_ && handle_->fd >= 0) {
        ::close(handle_->fd);
    }
}

Status SerialPort::writeLine(std::string_view line) {
    const std::string data = std::string(line) + "\n";
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t written = ::write(handle_->fd, data.data() + sent, data.size() - sent);
        if (written < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                pollfd ready{handle_->fd, POLLOUT, 0};
                (void)::poll(&ready, 1, 100);
                continue;
            }
            return portError(handle_->name, std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        }
        sent += static_cast<std::size_t>(written);
    }
    return success();
}

Result<std::optional<std::string>> SerialPort::readLine(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    for (;;) {
        if (auto line = buffer_.take()) {
            return line;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) {
            return std::optional<std::string>();
        }
        pollfd ready{handle_->fd, POLLIN, 0};
        const int status = ::poll(&ready, 1, static_cast<int>(left));
        if (status < 0 && errno != EINTR) {
            return portError(handle_->name, std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        }
        if (status <= 0) {
            continue;
        }
        if ((ready.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return portError(handle_->name, "the board was unplugged");
        }
        std::array<char, 256> chunk{};
        const ssize_t received = ::read(handle_->fd, chunk.data(), chunk.size());
        if (received > 0) {
            buffer_.append(std::string_view(chunk.data(), static_cast<std::size_t>(received)));
        } else if (received < 0 && errno != EAGAIN && errno != EINTR) {
            return portError(handle_->name, std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
        }
    }
}

#endif

SerialPort::SerialPort(std::unique_ptr<Handle> handle) : handle_(std::move(handle)) {}

Monitor::Monitor(std::unique_ptr<LineTransport> transport) : transport_(std::move(transport)) {
    snapshot_.open = true;
    thread_ = std::thread([this] { run(); });
}

Monitor::~Monitor() {
    stopping_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }
}

Result<std::unique_ptr<Monitor>> Monitor::open(const std::string& port, int baud) {
    auto opened = SerialPort::open(port, baud);
    if (!opened.ok()) {
        return opened.error();
    }
    return std::make_unique<Monitor>(std::move(opened).value());
}

void Monitor::send(std::string line) {
    const std::lock_guard<std::mutex> lock(mutex_);
    outgoing_.push_back(std::move(line));
}

Monitor::Snapshot Monitor::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

void Monitor::run() {
    const auto fail = [this](Error error) {
        const std::lock_guard<std::mutex> lock(mutex_);
        snapshot_.open = false;
        snapshot_.error = std::move(error);
    };
    while (!stopping_) {
        std::deque<std::string> outgoing;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            outgoing.swap(outgoing_);
        }
        for (const std::string& line : outgoing) {
            if (const auto sent = transport_->writeLine(line); !sent.ok()) {
                fail(sent.error());
                return;
            }
        }
        auto received = transport_->readLine(std::chrono::milliseconds(50));
        if (!received.ok()) {
            fail(received.error());
            return;
        }
        if (auto line = std::move(received).value()) {
            const std::lock_guard<std::mutex> lock(mutex_);
            snapshot_.lines.push_back(std::move(*line));
            ++snapshot_.count;
            if (snapshot_.lines.size() > kMaxLines) {
                snapshot_.lines.erase(snapshot_.lines.begin());
            }
        }
    }
}

} // namespace s2a::serial
