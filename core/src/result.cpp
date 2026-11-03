#include "s2a/result.hpp"

namespace s2a {

std::string_view errorMessageKey(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::FileNotFound:
        return "error.file_not_found";
    case ErrorCode::ReadFailed:
        return "error.read_failed";
    case ErrorCode::WriteFailed:
        return "error.write_failed";
    case ErrorCode::InvalidArchive:
        return "error.invalid_archive";
    case ErrorCode::InvalidJson:
        return "error.invalid_json";
    case ErrorCode::InvalidFormat:
        return "error.invalid_format";
    case ErrorCode::UnsupportedVersion:
        return "error.unsupported_version";
    case ErrorCode::InvalidCatalog:
        return "error.invalid_catalog";
    case ErrorCode::UnknownBoard:
        return "error.unknown_board";
    }
    return "error.unknown";
}

} // namespace s2a
