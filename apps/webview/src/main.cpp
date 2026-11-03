#include "welcome_page.hpp"

#include "s2a/i18n.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <webview/webview.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int run() {
    try {
        const s2a::Language language = s2a::detectSystemLanguage();
        const std::string title =
            s2a::tr("app.title", language) + " — " + s2a::tr("app.variant.webview", language);

        webview::webview window(false, nullptr);
        window.set_title(title);
        window.set_size(960, 640, WEBVIEW_HINT_NONE);
        window.set_html(s2a::webview::welcomePage(language));
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
    return run();
}
#else
int main() {
    return run();
}
#endif
