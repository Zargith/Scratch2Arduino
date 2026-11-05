#include "s2a/serial.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace {

using std::chrono::milliseconds;

/// Board printing lines; what the monitor sends is kept for the test.
struct FakeBoard {
    std::mutex mutex;
    std::deque<std::string> toSend;
    std::vector<std::string> received;
    bool unplugged = false;
};

struct FakeTransport final : s2a::serial::LineTransport {
    std::shared_ptr<FakeBoard> board;
    explicit FakeTransport(std::shared_ptr<FakeBoard> shared) : board(std::move(shared)) {}

    s2a::Status writeLine(std::string_view line) override {
        const std::lock_guard<std::mutex> lock(board->mutex);
        board->received.emplace_back(line);
        return s2a::success();
    }
    s2a::Result<std::optional<std::string>> readLine(milliseconds timeout) override {
        {
            const std::lock_guard<std::mutex> lock(board->mutex);
            if (board->unplugged) {
                return s2a::Error{s2a::ErrorCode::PortFailed, "unplugged"};
            }
            if (!board->toSend.empty()) {
                std::string line = board->toSend.front();
                board->toSend.pop_front();
                return std::optional<std::string>(line);
            }
        }
        std::this_thread::sleep_for(timeout / 10);
        return std::optional<std::string>();
    }
};

template <typename Predicate> bool waitFor(Predicate predicate) {
    for (int i = 0; i < 300; ++i) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(milliseconds(10));
    }
    return false;
}

TEST(Monitor, ReceivesAndSendsLines) {
    auto board = std::make_shared<FakeBoard>();
    board->toSend = {"bonjour", "512"};
    s2a::serial::Monitor monitor(std::make_unique<FakeTransport>(board));
    ASSERT_TRUE(waitFor([&] { return monitor.snapshot().count == 2; }));
    const auto snapshot = monitor.snapshot();
    EXPECT_TRUE(snapshot.open);
    EXPECT_EQ(snapshot.lines, (std::vector<std::string>{"bonjour", "512"}));

    monitor.send("allume");
    ASSERT_TRUE(waitFor([&] {
        const std::lock_guard<std::mutex> lock(board->mutex);
        return board->received.size() == 1;
    }));
    EXPECT_EQ(board->received.front(), "allume");
}

TEST(Monitor, KeepsTheLastLines) {
    auto board = std::make_shared<FakeBoard>();
    const std::size_t total = s2a::serial::Monitor::kMaxLines + 20;
    for (std::size_t i = 0; i < total; ++i) {
        board->toSend.push_back(std::to_string(i));
    }
    s2a::serial::Monitor monitor(std::make_unique<FakeTransport>(board));
    ASSERT_TRUE(waitFor([&] { return monitor.snapshot().count == total; }));
    const auto snapshot = monitor.snapshot();
    ASSERT_EQ(snapshot.lines.size(), s2a::serial::Monitor::kMaxLines);
    EXPECT_EQ(snapshot.lines.front(), "20");
    EXPECT_EQ(snapshot.lines.back(), std::to_string(total - 1));
}

TEST(Monitor, ReportsAnUnpluggedBoard) {
    auto board = std::make_shared<FakeBoard>();
    s2a::serial::Monitor monitor(std::make_unique<FakeTransport>(board));
    {
        const std::lock_guard<std::mutex> lock(board->mutex);
        board->unplugged = true;
    }
    ASSERT_TRUE(waitFor([&] { return !monitor.snapshot().open; }));
    const auto snapshot = monitor.snapshot();
    ASSERT_TRUE(snapshot.error.has_value());
    EXPECT_EQ(snapshot.error->code, s2a::ErrorCode::PortFailed);
}

} // namespace
