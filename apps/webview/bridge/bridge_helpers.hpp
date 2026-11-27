#pragma once

#include <string>
#include <string_view>

/// Pure helpers of the bridge, apart so that the tests can call them (audit #197).
namespace s2a::webview::detail {

/// Bytes of a base64 text (a "data:...;base64," prefix is skipped); invalid characters are ignored.
[[nodiscard]] std::string base64Decode(std::string_view text);
/// Base64 text of some bytes (with padding).
[[nodiscard]] std::string base64Encode(std::string_view bytes);
/// An installer of a release of the project: the only files the in-app update downloads.
[[nodiscard]] bool isReleaseInstaller(const std::string& url);
/// A page of the project's repository: the only links the editor may open in the browser.
[[nodiscard]] bool isProjectPage(const std::string& url);

} // namespace s2a::webview::detail
