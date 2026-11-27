#pragma once

#include <functional>
#include <string>

namespace s2a::webview {

/// What the bridge needs from the window: the webview in the application, a fake one in the tests
/// (audit #197). Calls can come from any thread, as webview allows for resolve() and dispatch().
class HostWindow {
public:
    /// Called with the id of the JavaScript promise and the call's arguments (a JSON array).
    using Callback = std::function<void(const std::string& id, const std::string& request)>;

    HostWindow() = default;
    HostWindow(const HostWindow&) = delete;
    HostWindow& operator=(const HostWindow&) = delete;
    HostWindow(HostWindow&&) = delete;
    HostWindow& operator=(HostWindow&&) = delete;
    virtual ~HostWindow() = default;

    /// Offers the JavaScript function `name` to the page.
    virtual void bind(const std::string& name, Callback callback) = 0;
    /// Settles the promise `id`: fulfilled with the JSON `result` when `ok`, rejected otherwise.
    virtual void resolve(const std::string& id, bool ok, const std::string& result) = 0;
    virtual void setTitle(const std::string& title) = 0;
    /// Runs `work` on the UI thread.
    virtual void dispatch(std::function<void()> work) = 0;
    /// Ends the window's event loop (the program then exits).
    virtual void terminate() = 0;
};

} // namespace s2a::webview
