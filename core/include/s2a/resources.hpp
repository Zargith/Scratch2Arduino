#pragma once

#include <optional>
#include <string_view>
#include <vector>

namespace s2a::resources {

/// A data file compiled into the binary (see cmake/S2AEmbedResources.cmake).
struct Resource {
    std::string_view path; ///< Path relative to the repository root, e.g. "resources/i18n/fr.json".
    std::string_view data; ///< File content (UTF-8 text for every file shipped today).
};

/// Every embedded resource, sorted by path.
[[nodiscard]] const std::vector<Resource>& all();

/// The resource stored at `path`, if any.
[[nodiscard]] std::optional<std::string_view> find(std::string_view path);

/// Resources whose path starts with `prefix` (e.g. "resources/boards/"), sorted by path.
[[nodiscard]] std::vector<Resource> list(std::string_view prefix);

namespace detail {
/// Implemented by the generated file.
std::vector<Resource> embeddedResources();
} // namespace detail

} // namespace s2a::resources
