#include "welcome_page.hpp"

#include "s2a/version.hpp"

#include <string_view>

namespace s2a::webview {

namespace {

// Escapes text inserted into HTML element content.
std::string escapeHtml(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

constexpr std::string_view kStyle = R"css(
:root { color-scheme: light dark; --bg: #f4f7fb; --fg: #1f2937; --accent: #00878f; --block: #ff8c1a; }
@media (prefers-color-scheme: dark) { :root { --bg: #111827; --fg: #e5e7eb; --accent: #2bb7bf; } }
html, body { margin: 0; height: 100%; }
body { display: flex; align-items: center; justify-content: center; background: var(--bg);
       color: var(--fg); font-family: system-ui, -apple-system, "Segoe UI", sans-serif; }
main { text-align: center; padding: 2rem; max-width: 36rem; }
h1 { color: var(--accent); font-size: 2.5rem; margin: 0 0 1rem; }
p { font-size: 1.15rem; line-height: 1.5; margin: 0 0 1rem; }
.badge { display: inline-block; padding: .3rem .9rem; border-radius: 999px; background: var(--block);
         color: #fff; font-weight: 600; }
)css";

} // namespace

std::string welcomePage(Language language, std::string_view message) {
    std::string html;
    html += "<!doctype html><html lang=\"";
    html += languageCode(language);
    html += "\"><head><meta charset=\"utf-8\"><title>";
    html += escapeHtml(tr("app.title", language));
    html += "</title><style>";
    html += kStyle;
    html += "</style></head><body><main><h1>";
    html += escapeHtml(tr("app.title", language));
    html += "</h1><p>";
    html += escapeHtml(tr("app.welcome", language));
    html += "</p><p>";
    html += escapeHtml(message);
    html += "</p><span class=\"badge\">";
    html += escapeHtml(tr("app.variant.webview", language));
    html += " · v";
    html += version::kString;
    html += "</span></main></body></html>";
    return html;
}

} // namespace s2a::webview
