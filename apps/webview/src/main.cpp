#include "bridge.hpp"
#include "welcome_page.hpp"

#include "s2a/i18n.hpp"

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <webview/webview.h>

#ifdef _WIN32
#include <windows.h>
// After windows.h, which it needs.
#include <shellapi.h>
#endif

namespace {

/// Opens the window as large as the screen: some blocks are long (16-pin LCD...) and the
/// palette and the scripts need room. The user can still make it smaller.
void maximize(webview::webview& window) {
    const auto handle = window.window();
    if (!handle.ok() || handle.value() == nullptr) {
        return;
    }
#if defined(_WIN32)
    ShowWindow(static_cast<HWND>(handle.value()), SW_MAXIMIZE);
#elif defined(__APPLE__)
    webview::detail::objc::msg_send<void>(static_cast<id>(handle.value()), sel_registerName("zoom:"), nullptr);
#else
    gtk_window_maximize(GTK_WINDOW(handle.value()));
#endif
}

int run(const std::filesystem::path& startupFile) {
    try {
        const s2a::Language language = s2a::detectSystemLanguage();
        const std::string title = s2a::tr("app.title", language);

        webview::webview window(false, nullptr);
        window.set_title(title);
        window.set_size(1280, 800, WEBVIEW_HINT_NONE);
        window.set_size(960, 600, WEBVIEW_HINT_MIN);
        maximize(window);

        s2a::webview::Bridge bridge(window, language);
        bridge.bind();
        if (!startupFile.empty()) {
            bridge.setStartupFile(startupFile);
        }
        if (const auto frontend = s2a::webview::locateFrontend(); !frontend.empty()) {
            window.navigate(s2a::webview::fileUrl(frontend / "index.html"));
        } else {
            window.set_html(s2a::webview::welcomePage(language, s2a::tr("webview.frontend_missing", language)));
        }
        window.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scratch2Arduino: " << error.what() << '\n';
        return 1;
    }
}

} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE /*instance*/, HINSTANCE /*previous*/, LPSTR /*cmdLine*/,
                   int /*showCmd*/) {
    // Lets the installer see that the program is open before updating it (AppMutex).
    HANDLE running = CreateMutexW(nullptr, FALSE, L"Scratch2ArduinoRunning");
    // The project dropped on the icon or double-clicked, in UTF-16 (any accent in its path).
    std::filesystem::path startupFile;
    int count = 0;
    if (LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count)) {
        if (count > 1) {
            startupFile = std::filesystem::path(arguments[1]);
        }
        LocalFree(arguments);
    }
    const int status = run(startupFile);
    if (running != nullptr) {
        CloseHandle(running);
    }
    return status;
}
#else
int main(int argc, char** argv) {
    return run(argc > 1 ? std::filesystem::u8path(argv[1]) : std::filesystem::path{});
}
#endif
