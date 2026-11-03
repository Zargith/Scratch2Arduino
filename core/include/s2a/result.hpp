#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace s2a {

/// Why an operation failed. Every code has a translated, beginner-friendly message
/// (key returned by errorMessageKey()), shown at Build/Run or when opening a file - never
/// while the user is editing.
enum class ErrorCode {
    FileNotFound,
    ReadFailed,
    WriteFailed,
    InvalidArchive,
    InvalidJson,
    InvalidFormat,
    UnsupportedVersion,
    InvalidCatalog,
    UnknownBoard,
    ProcessFailed,
};

/// Translation key of the user-facing message for `code` (e.g. "error.file_not_found").
[[nodiscard]] std::string_view errorMessageKey(ErrorCode code) noexcept;

struct Error {
    ErrorCode code;
    std::string detail; ///< Technical detail in English, for logs and bug reports.
};

/// Either a value or an Error. Used instead of exceptions for anything that depends on user files.
template <typename T> class [[nodiscard]] Result {
public:
    Result(T value) : content_(std::move(value)) {}          // NOLINT(google-explicit-constructor)
    Result(Error error) : content_(std::move(error)) {}      // NOLINT(google-explicit-constructor)

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(content_); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & { return std::get<T>(content_); }
    [[nodiscard]] const T& value() const& { return std::get<T>(content_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(content_)); }

    [[nodiscard]] const Error& error() const& { return std::get<Error>(content_); }

private:
    std::variant<T, Error> content_;
};

/// Result of an operation that produces no value.
using Status = Result<std::monostate>;

[[nodiscard]] inline Status success() {
    return std::monostate{};
}

} // namespace s2a
