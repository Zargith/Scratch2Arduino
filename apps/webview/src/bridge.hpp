#pragma once

#include "s2a/i18n.hpp"

#include <atomic>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <webview/webview.h>
// On Linux webview.h pulls in GTK, hence X11, whose macros clash with ordinary names
// (s2a::Status, Problem::None...).
#undef Bool
#undef None
#undef Status
#undef Success

namespace s2a::webview {

/// JavaScript functions offered to the frontend (frontend/src/bridge.ts). Slow calls
/// (dialogs, arduino-cli) run on worker threads and resolve their promise when done.
class Bridge {
public:
    Bridge(::webview::webview& window, Language language);
    Bridge(const Bridge&) = delete;
    Bridge& operator=(const Bridge&) = delete;
    Bridge(Bridge&&) = delete;
    Bridge& operator=(Bridge&&) = delete;
    /// Waits for running calls (an upload in progress finishes before the app exits).
    ~Bridge();

    void bind();

private:
    template <typename Work> void async(const std::string& id, Work work);

    ::webview::webview& window_;
    Language language_;
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex mutex_;
    std::list<Worker> workers_; ///< Finished ones are joined when the next call starts.
};

/// Folder holding index.html of the frontend, or empty when it cannot be found.
[[nodiscard]] std::filesystem::path locateFrontend();

/// file:// URL of a local file.
[[nodiscard]] std::string fileUrl(const std::filesystem::path& file);

} // namespace s2a::webview
