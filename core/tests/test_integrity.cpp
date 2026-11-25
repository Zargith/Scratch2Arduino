#include "s2a/integrity.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace s2a::integrity;

// Audit 2026-11-24 (#174): the updater ran the downloaded installer without any integrity check.
TEST(Integrity, HashesLikeSha256) {
    // FIPS 180-2 examples.
    EXPECT_EQ(sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(sha256Hex(std::string(1000000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Integrity, PadsEveryLengthAroundABlock) {
    // Lengths around the 55 / 56 / 64 byte padding limits, against hashes from sha256sum.
    const std::pair<std::size_t, const char*> cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    };
    for (const auto& [length, expected] : cases) {
        EXPECT_EQ(sha256Hex(std::string(length, 'a')), expected) << length;
    }
}

TEST(Integrity, HashesAFile) {
    const fs::path file = fs::temp_directory_path() / "s2a-integrity-test.bin";
    {
        std::ofstream out(file, std::ios::binary);
        out << std::string(200000, 'a') << "abc";
    }
    const auto hash = sha256File(file);
    ASSERT_TRUE(hash.ok());
    EXPECT_EQ(hash.value(), sha256Hex(std::string(200000, 'a') + "abc"));
    fs::remove(file);
    EXPECT_FALSE(sha256File(file).ok());
}

TEST(Integrity, FindsTheHashOfAFileInSha256Sums) {
    const std::string sums = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  Scratch2Arduino_installer.exe\n"
                             "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 *scratch2arduino_1.3.5_amd64.deb\r\n"
                             "not a line\n";
    EXPECT_EQ(expectedHash(sums, "Scratch2Arduino_installer.exe"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(expectedHash(sums, "scratch2arduino_1.3.5_amd64.deb"),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"); // lower case
    EXPECT_FALSE(expectedHash(sums, "other.exe").has_value());
    EXPECT_FALSE(expectedHash(sums, "installer.exe").has_value()); // whole names only
    EXPECT_FALSE(expectedHash("abc  Scratch2Arduino_installer.exe\n", "Scratch2Arduino_installer.exe").has_value());
    EXPECT_FALSE(expectedHash(sums + "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  Scratch2Arduino_installer.exe\n",
                              "Scratch2Arduino_installer.exe")
                     .has_value()); // listed twice with different hashes
    EXPECT_EQ(expectedHash("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  my file.exe\n", "my file.exe"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Integrity, VerifiesADownloadAgainstTheList) {
    const fs::path file = fs::temp_directory_path() / "s2a-integrity-verify.exe";
    {
        std::ofstream out(file, std::ios::binary);
        out << "abc";
    }
    const std::string sums = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  good.exe\n"
                             "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  tampered.exe\n";
    EXPECT_TRUE(verifyFile(file, sums, "good.exe").ok());
    EXPECT_EQ(verifyFile(file, sums, "tampered.exe").error().code, s2a::ErrorCode::InvalidArchive);
    EXPECT_EQ(verifyFile(file, sums, "missing.exe").error().code, s2a::ErrorCode::InvalidFormat);
    fs::remove(file);
    EXPECT_EQ(verifyFile(file, sums, "good.exe").error().code, s2a::ErrorCode::ReadFailed);
}

} // namespace
