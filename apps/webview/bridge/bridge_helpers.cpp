#include "bridge_helpers.hpp"

#include <algorithm>
#include <cctype>

namespace s2a::webview::detail {

/// Bytes of a base64 text (a "data:...;base64," prefix is skipped); invalid characters are ignored.
std::string base64Decode(std::string_view text) {
    if (const auto comma = text.find(','); text.substr(0, 5) == "data:" && comma != std::string_view::npos) {
        text.remove_prefix(comma + 1);
    }
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string bytes;
    unsigned int buffer = 0;
    int bits = 0;
    for (const char c : text) {
        const auto value = kAlphabet.find(c);
        if (value == std::string_view::npos) {
            continue; // padding, line breaks
        }
        buffer = (buffer << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return bytes;
}

/// Base64 text of some bytes (with padding).
std::string base64Encode(std::string_view bytes) {
    static constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t count = std::min<std::size_t>(3, bytes.size() - i);
        unsigned int chunk = 0;
        for (std::size_t j = 0; j < 3; ++j) {
            chunk = (chunk << 8) | (j < count ? static_cast<unsigned char>(bytes[i + j]) : 0U);
        }
        for (std::size_t j = 0; j < 4; ++j) {
            text.push_back(j <= count ? kAlphabet[(chunk >> (18 - 6 * j)) & 0x3F] : '=');
        }
    }
    return text;
}

/// An installer of a release of the project ("https://github.com/Zargith/Scratch2Arduino/releases/
/// download/v0.11.1/Scratch2Arduino_installer.exe"): the only files the update downloads.
bool isReleaseInstaller(const std::string& url) {
    static const std::string kDownloads = "https://github.com/Zargith/Scratch2Arduino/releases/download/";
    if (url.rfind(kDownloads, 0) != 0 || url.size() > 300 || url.size() < 5 ||
        url.compare(url.size() - 4, 4, ".exe") != 0 || url.find("..") != std::string::npos) {
        return false;
    }
    return std::all_of(url.begin(), url.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || std::string_view("/:.-_").find(c) != std::string_view::npos;
    });
}

/// A page of the project's repository, with only plain URL characters: the only links the editor
/// may open in the browser.
bool isProjectPage(const std::string& url) {
    static const std::string kRepository = "https://github.com/Zargith/Scratch2Arduino/";
    if (url.rfind(kRepository, 0) != 0 || url.size() > 512) {
        return false;
    }
    return std::all_of(url.begin(), url.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
               std::string_view("/:.-_~%?=&#+").find(c) != std::string_view::npos;
    });
}
} // namespace s2a::webview::detail
