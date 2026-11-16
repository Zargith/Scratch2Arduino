// project.json as stored inside a .s2a archive (private to the core: used by archive.cpp).
#pragma once

#include "s2a/project.hpp"

#include <map>
#include <string>
#include <string_view>

namespace s2a::project::detail {

/// project.json of an archive and the code files it references (name -> text).
struct ArchiveDocument {
    std::string json;
    std::map<std::string, std::string> files;
};

/// project.json for a .s2a archive: the code is moved to its own files, "savedWith" is added.
[[nodiscard]] ArchiveDocument toArchiveDocument(const Project& project);

/// Parses the project.json of an archive. `files` holds the entries at the root of the archive,
/// where the code files named in project.json are looked up.
[[nodiscard]] Result<Project> fromArchiveDocument(std::string_view json, const std::map<std::string, std::string>& files);

} // namespace s2a::project::detail
