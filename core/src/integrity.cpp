#include "s2a/integrity.hpp"

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace s2a::integrity {

namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotateRight(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

/// SHA-256 (FIPS 180-4), fed in pieces.
class Sha256 {
public:
    void add(std::string_view data) {
        for (const char c : data) {
            block_[used_++] = static_cast<std::uint8_t>(c);
            if (used_ == block_.size()) {
                compress();
                used_ = 0;
            }
        }
        length_ += data.size();
    }

    std::string hex() {
        const std::uint64_t bits = length_ * 8;
        block_[used_++] = 0x80;
        if (used_ > 56) {
            while (used_ < block_.size()) {
                block_[used_++] = 0;
            }
            compress();
            used_ = 0;
        }
        while (used_ < 56) {
            block_[used_++] = 0;
        }
        for (int i = 7; i >= 0; --i) {
            block_[used_++] = static_cast<std::uint8_t>(bits >> (i * 8));
        }
        compress();
        static const char* const kDigits = "0123456789abcdef";
        std::string result;
        for (const std::uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                result += kDigits[(word >> shift) & 0xF];
            }
        }
        return result;
    }

private:
    void compress() {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (std::uint32_t{block_[i * 4]} << 24) | (std::uint32_t{block_[i * 4 + 1]} << 16) |
                   (std::uint32_t{block_[i * 4 + 2]} << 8) | std::uint32_t{block_[i * 4 + 3]};
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotateRight(w[i - 15], 7) ^ rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(w[i - 2], 17) ^ rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::array<std::uint32_t, 8> v = state_;
        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotateRight(v[4], 6) ^ rotateRight(v[4], 11) ^ rotateRight(v[4], 25);
            const std::uint32_t choice = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const std::uint32_t t1 = v[7] + s1 + choice + kRound[i] + w[i];
            const std::uint32_t s0 = rotateRight(v[0], 2) ^ rotateRight(v[0], 13) ^ rotateRight(v[0], 22);
            const std::uint32_t majority = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const std::uint32_t t2 = s0 + majority;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (std::size_t i = 0; i < 8; ++i) {
            state_[i] += v[i];
        }
    }

    std::array<std::uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block_{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

bool isHexDigest(std::string_view text) {
    if (text.size() != 64) {
        return false;
    }
    for (const char c : text) {
        if (std::isxdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string sha256Hex(std::string_view data) {
    Sha256 hash;
    hash.add(data);
    return hash.hex();
}

Result<std::string> sha256File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error{ErrorCode::ReadFailed, "cannot read " + path.u8string()};
    }
    Sha256 hash;
    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        hash.add(std::string_view(buffer.data(), static_cast<std::size_t>(in.gcount())));
    }
    if (in.bad()) {
        return Error{ErrorCode::ReadFailed, "cannot read " + path.u8string()};
    }
    return hash.hex();
}

std::optional<std::string> expectedHash(std::string_view sums, std::string_view fileName) {
    std::istringstream lines{std::string(sums)};
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto space = line.find(' ');
        if (space == std::string::npos || !isHexDigest(std::string_view(line).substr(0, space))) {
            continue;
        }
        std::string_view name = std::string_view(line).substr(space);
        while (!name.empty() && name.front() == ' ') {
            name.remove_prefix(1);
        }
        if (!name.empty() && name.front() == '*') {
            name.remove_prefix(1);
        }
        if (name == fileName) {
            std::string digest = line.substr(0, space);
            for (char& c : digest) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return digest;
        }
    }
    return std::nullopt;
}

Status verifyFile(const std::filesystem::path& file, std::string_view sums, std::string_view fileName) {
    const auto expected = expectedHash(sums, fileName);
    if (!expected) {
        return Error{ErrorCode::InvalidFormat, std::string(fileName) + " is not in the list of hashes"};
    }
    auto actual = sha256File(file);
    if (!actual.ok()) {
        return actual.error();
    }
    if (actual.value() != *expected) {
        return Error{ErrorCode::InvalidArchive, std::string(fileName) + " does not match its published hash"};
    }
    return success();
}

} // namespace s2a::integrity
