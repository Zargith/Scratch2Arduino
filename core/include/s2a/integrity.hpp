#pragma once

#include "s2a/result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

/// Checks that a downloaded file is the one published (the update installer): SHA-256 and the
/// SHA256SUMS.txt list attached to each release.
namespace s2a::integrity {

/// SHA-256 of `data`, as 64 lower-case hexadecimal digits.
[[nodiscard]] std::string sha256Hex(std::string_view data);

/// SHA-256 of the file at `path` (read in chunks).
[[nodiscard]] Result<std::string> sha256File(const std::filesystem::path& path);

/// Hash given for `fileName` in a SHA256SUMS text (lines "<64 hex digits>  <name>", or with "*"
/// before the name as sha256sum -b writes), in lower case; nothing when the file is not listed.
[[nodiscard]] std::optional<std::string> expectedHash(std::string_view sums, std::string_view fileName);

/// Checks that `file` has the hash given for `fileName` in `sums`. Fails with InvalidFormat when
/// the file is not listed, ReadFailed when it cannot be read, InvalidArchive when it differs.
[[nodiscard]] Status verifyFile(const std::filesystem::path& file, std::string_view sums, std::string_view fileName);

} // namespace s2a::integrity
