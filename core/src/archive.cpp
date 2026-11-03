// .s2a archives: a zip file holding project.json and the project assets.
#include "s2a/project.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <miniz.h>
#include <system_error>

namespace s2a::project {

namespace {

// Limits protecting against corrupted or malicious archives (zip bombs).
constexpr std::uint64_t kMaxEntrySize = 64ULL * 1024 * 1024;
constexpr std::uint64_t kMaxTotalSize = 256ULL * 1024 * 1024;
constexpr std::uintmax_t kMaxFileSize = 256ULL * 1024 * 1024;

/// RAII wrapper around a miniz archive.
class Zip {
public:
    Zip() { mz_zip_zero_struct(&archive_); }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    Zip(Zip&&) = delete;
    Zip& operator=(Zip&&) = delete;
    ~Zip() {
        if (writing_) {
            mz_zip_writer_end(&archive_);
        } else {
            mz_zip_reader_end(&archive_);
        }
    }

    mz_zip_archive* get() noexcept { return &archive_; }
    void setWriting() noexcept { writing_ = true; }
    [[nodiscard]] std::string lastError() { return mz_zip_get_error_string(mz_zip_get_last_error(&archive_)); }

private:
    mz_zip_archive archive_{};
    bool writing_ = false;
};

/// Asset names must stay inside assets/ once extracted (no "..", absolute paths or backslashes).
bool isSafeAssetName(std::string_view name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string_view::npos ||
        name.find(':') != std::string_view::npos || name.back() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t end = std::min(name.find('/', start), name.size());
        const std::string_view part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

} // namespace

Result<std::string> toArchive(const Project& project) {
    Zip zip;
    if (!mz_zip_writer_init_heap(zip.get(), 0, 0)) {
        return Error{ErrorCode::WriteFailed, "cannot create zip archive: " + zip.lastError()};
    }
    zip.setWriting();

    const auto add = [&](const std::string& name, std::string_view data) {
        return mz_zip_writer_add_mem_ex(zip.get(), name.c_str(), data.data(), data.size(), nullptr, 0,
                                        MZ_DEFAULT_LEVEL, 0, 0) != 0;
    };
    // Enforce the limits fromArchive() applies, so a saved project can always be reopened.
    const std::string json = toJson(project);
    std::uint64_t total = json.size();
    if (json.size() > kMaxEntrySize) {
        return Error{ErrorCode::WriteFailed, "project.json is too large"};
    }
    for (const auto& [name, data] : project.assets) {
        total += data.size();
        if (data.size() > kMaxEntrySize || total > kMaxTotalSize) {
            return Error{ErrorCode::WriteFailed, "asset '" + name + "' makes the project too large"};
        }
    }
    if (!add(std::string(kProjectEntry), json)) {
        return Error{ErrorCode::WriteFailed, "cannot add project.json: " + zip.lastError()};
    }
    for (const auto& [name, data] : project.assets) {
        if (!isSafeAssetName(name)) {
            return Error{ErrorCode::WriteFailed, "invalid asset name '" + name + "'"};
        }
        if (!add(std::string(kAssetsPrefix) + name, data)) {
            return Error{ErrorCode::WriteFailed, "cannot add asset '" + name + "': " + zip.lastError()};
        }
    }

    void* buffer = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(zip.get(), &buffer, &size)) {
        return Error{ErrorCode::WriteFailed, "cannot finalize zip archive: " + zip.lastError()};
    }
    std::string bytes(static_cast<const char*>(buffer), size);
    mz_free(buffer);
    if (bytes.size() > kMaxFileSize) {
        return Error{ErrorCode::WriteFailed, "the archive is too large"};
    }
    return bytes;
}

Result<Project> fromArchive(std::string_view bytes) {
    Zip zip;
    if (!mz_zip_reader_init_mem(zip.get(), bytes.data(), bytes.size(), 0)) {
        return Error{ErrorCode::InvalidArchive, "not a zip archive: " + zip.lastError()};
    }

    std::string json;
    bool foundProject = false;
    std::map<std::string, std::string> assets;
    std::uint64_t total = 0;
    const mz_uint count = mz_zip_reader_get_num_files(zip.get());
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(zip.get(), index, &stat)) {
            return Error{ErrorCode::InvalidArchive, "unreadable zip entry: " + zip.lastError()};
        }
        if (stat.m_is_directory) {
            continue;
        }
        // m_filename is truncated to a fixed-size buffer: read the complete name.
        const mz_uint nameSize = mz_zip_reader_get_filename(zip.get(), index, nullptr, 0);
        std::string name(nameSize, '\0');
        if (nameSize == 0 || mz_zip_reader_get_filename(zip.get(), index, name.data(), nameSize) != nameSize) {
            return Error{ErrorCode::InvalidArchive, "unreadable zip entry name"};
        }
        name.pop_back(); // terminating NUL
        const bool isProject = name == kProjectEntry;
        const bool isAsset = name.rfind(std::string(kAssetsPrefix), 0) == 0;
        if (!isProject && !isAsset) {
            continue; // Unknown entries are ignored, for forward compatibility.
        }
        total += stat.m_uncomp_size;
        if (stat.m_uncomp_size > kMaxEntrySize || total > kMaxTotalSize) {
            return Error{ErrorCode::InvalidArchive, "archive entry '" + name + "' is too large"};
        }
        std::string content(static_cast<std::size_t>(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(zip.get(), index, content.data(), content.size(), 0)) {
            return Error{ErrorCode::InvalidArchive, "cannot extract '" + name + "': " + zip.lastError()};
        }
        if (isProject) {
            json = std::move(content);
            foundProject = true;
        } else {
            const std::string assetName = name.substr(kAssetsPrefix.size());
            if (!isSafeAssetName(assetName)) {
                return Error{ErrorCode::InvalidArchive, "invalid asset name '" + name + "'"};
            }
            assets[assetName] = std::move(content);
        }
    }
    if (!foundProject) {
        return Error{ErrorCode::InvalidArchive, "the archive has no project.json"};
    }

    auto project = fromJson(json);
    if (project.ok()) {
        project.value().assets = std::move(assets);
    }
    return project;
}

Status save(const Project& project, const std::filesystem::path& path) {
    auto bytes = toArchive(project);
    if (!bytes.ok()) {
        return bytes.error();
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Error{ErrorCode::WriteFailed, "cannot create " + temporary.u8string()};
        }
        out.write(bytes.value().data(), static_cast<std::streamsize>(bytes.value().size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return Error{ErrorCode::WriteFailed, "cannot write " + temporary.u8string()};
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Error{ErrorCode::WriteFailed, "cannot replace " + path.u8string() + ": " + error.message()};
    }
    return success();
}

Result<Project> load(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return Error{ErrorCode::FileNotFound, path.u8string() + " does not exist"};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return Error{ErrorCode::ReadFailed, "cannot read " + path.u8string() + ": " + error.message()};
    }
    if (size > kMaxFileSize) {
        return Error{ErrorCode::InvalidArchive, path.u8string() + " is too large to be a project"};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error{ErrorCode::ReadFailed, "cannot open " + path.u8string()};
    }
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return Error{ErrorCode::ReadFailed, "cannot read " + path.u8string()};
    }
    return fromArchive(bytes);
}

} // namespace s2a::project
