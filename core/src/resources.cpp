#include "s2a/resources.hpp"

#include <algorithm>

namespace s2a::resources {

const std::vector<Resource>& all() {
    static const std::vector<Resource> sorted = [] {
        std::vector<Resource> resources = detail::embeddedResources();
        std::sort(resources.begin(), resources.end(),
                  [](const Resource& a, const Resource& b) { return a.path < b.path; });
        return resources;
    }();
    return sorted;
}

std::optional<std::string_view> find(std::string_view path) {
    const auto& resources = all();
    const auto it = std::lower_bound(
        resources.begin(), resources.end(), path,
        [](const Resource& resource, std::string_view value) { return resource.path < value; });
    if (it != resources.end() && it->path == path) {
        return it->data;
    }
    return std::nullopt;
}

std::vector<Resource> list(std::string_view prefix) {
    std::vector<Resource> matches;
    for (const Resource& resource : all()) {
        if (resource.path.substr(0, prefix.size()) == prefix) {
            matches.push_back(resource);
        }
    }
    return matches;
}

} // namespace s2a::resources
