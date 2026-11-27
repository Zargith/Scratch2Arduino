#pragma once

#include "s2a/i18n.hpp"
#include "s2a/live.hpp"
#include "s2a/serial.hpp"
#include "s2a/simulator.hpp"
#include "s2a/userdata.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <filesystem>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "host_window.hpp"

namespace s2a::webview {

/// JavaScript functions offered to the frontend (frontend/src/bridge.ts). Slow calls
/// (dialogs, arduino-cli) run on worker threads and resolve their promise when done.
class Bridge {
public:
    Bridge(HostWindow& window, Language language);
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge(Bridge&&) = delete;
    Bridge& operator=(Bridge&&) = delete;
    /// Waits for running calls (an upload in progress finishes before the app exits).
    ~Bridge();

    void bind();

    /// Project given to the program (file dropped on its icon, double-clicked .s2a file):
    /// the editor opens it once it has started.
    void setStartupFile(std::filesystem::path path) { startupFile_ = std::move(path); }

private:
    /// What "open" returns to the editor for the project at `path`; adds it to the recent projects.
    [[nodiscard]] std::string openedJson(const std::filesystem::path& path, Language language);

    std::filesystem::path startupFile_;
    template <typename Work> void async(const std::string& id, Work work);
    /// Binds a call that answers at once; any exception rejects its promise instead of ending the program.
    template <typename Handler> void bindSync(const std::string& name, Handler handler);
    /// Binds a call that uses the board's port (serial monitor, Live start and stop): it runs on
    /// the port thread, one call after the other in the order they were made, off the UI thread.
    template <typename Handler> void bindPortCall(const std::string& name, Handler handler);
    /// Runs the queued port calls; ends once stopping and empty.
    void runPortQueue();
    std::mutex portMutex_;
    std::condition_variable portReady_;
    std::deque<std::function<void()>> portJobs_; ///< Guarded by portMutex_.
    bool portStopping_ = false;                  ///< Guarded by portMutex_.
    std::thread portThread_;

    HostWindow& window_;
    Language language_;
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex mutex_;
    std::list<Worker> workers_; ///< Finished ones are joined when the next call starts.
    std::mutex liveMutex_;
    std::unique_ptr<live::Session> live_; ///< Live mode, when started.
    /// Teardown of the last stopped session (outputs off, port closed), guarded by liveMutex_:
    /// whatever needs the port again waits for it (audit #167).
    std::shared_future<void> liveClosing_;
    /// Virtual board of the simulation (Live mode on a "sim:" port), kept after Stop to show it off.
    std::shared_ptr<sim::VirtualBoard> simBoard_; ///< Guarded by liveMutex_.
    std::mutex monitorMutex_;
    std::unique_ptr<serial::Monitor> monitor_; ///< Serial monitor, when open.
    std::mutex userMutex_;
    userdata::Store user_; ///< Recent projects and autosave (guarded by userMutex_).
    /// Library manager: one arduino-cli library command at a time; the list of the registry is
    /// downloaded at the first search of the session.
    std::mutex librariesMutex_;
    bool libraryIndexReady_ = false; ///< Guarded by librariesMutex_.
    std::mutex packagesMutex_; ///< One board package installed or removed at a time.

    /// Frees the board's port for an upload or the Live mode.
    void closeMonitor();
    /// Stops the Live session off the UI thread (liveMutex_ held). Teardowns run one after the other.
    void retireLive();
    /// Waits until the stopped Live sessions have switched off their outputs and closed the port.
    void waitForLiveClosed();
    /// The board on the USB `port`, running the Live firmware (uploaded first when it does not
    /// answer): for settings saved on the board. Stops the Live mode and the serial monitor.
    [[nodiscard]] std::unique_ptr<serial::SerialPort> openLiveOverUsb(const Board& board, const std::string& port,
                                                                      Language language);
};

/// Folder holding index.html of the frontend, or empty when it cannot be found.
[[nodiscard]] std::filesystem::path locateFrontend();

/// file:// URL of a local file.
[[nodiscard]] std::string fileUrl(const std::filesystem::path& file);

} // namespace s2a::webview
