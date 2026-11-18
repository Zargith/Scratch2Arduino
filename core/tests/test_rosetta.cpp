#include "s2a/i18n.hpp"
#include "s2a/rosetta.hpp"

#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using s2a::process::Command;
using s2a::process::Output;
using s2a::rosetta::InstallResult;
using s2a::toolchain::HostSystem;

/// Answers "arch -x86_64" with `archWorks` (flipped to true once osascript succeeds) and osascript
/// with `install`.
struct FakeMac {
    bool archWorks = false;
    Output install{0, "", "", false};
    std::vector<std::string> programs;

    s2a::process::Runner runner() {
        return [this](const Command& command) -> s2a::Result<Output> {
            programs.push_back(command.program.string());
            if (command.program == "/usr/bin/arch") {
                return Output{archWorks ? 0 : 1, "", archWorks ? "" : "arch: Bad CPU type in executable", false};
            }
            if (install.exitCode == 0) {
                archWorks = true;
            }
            return install;
        };
    }
};

TEST(Rosetta, OnlyMattersOnMacsWithAnAppleChip) {
    FakeMac mac;
    EXPECT_TRUE(s2a::rosetta::missing(mac.runner(), HostSystem::MacArm64));
    for (const HostSystem other : {HostSystem::MacX64, HostSystem::WindowsX64, HostSystem::LinuxX64, HostSystem::LinuxArm64}) {
        EXPECT_FALSE(s2a::rosetta::missing(mac.runner(), other));
    }
    mac.archWorks = true;
    EXPECT_FALSE(s2a::rosetta::missing(mac.runner(), HostSystem::MacArm64));
}

TEST(Rosetta, InstallsBehindThePasswordPrompt) {
    FakeMac mac;
    EXPECT_EQ(s2a::rosetta::install(mac.runner(), HostSystem::MacArm64), InstallResult::Installed);
    ASSERT_EQ(mac.programs.size(), 3U); // check, install, check again
    EXPECT_EQ(mac.programs[1], "/usr/bin/osascript");

    FakeMac already;
    already.archWorks = true;
    EXPECT_EQ(s2a::rosetta::install(already.runner(), HostSystem::MacArm64), InstallResult::Installed);
    EXPECT_EQ(already.programs.size(), 1U); // nothing to install
}

TEST(Rosetta, CancelledOrFailed) {
    FakeMac cancelled;
    cancelled.install = Output{1, "", "execution error: User canceled. (-128)", false};
    EXPECT_EQ(s2a::rosetta::install(cancelled.runner(), HostSystem::MacArm64), InstallResult::Cancelled);

    FakeMac offline;
    offline.install = Output{1, "", "execution error: Install failed with error: An error has occurred. (1)", false};
    EXPECT_EQ(s2a::rosetta::install(offline.runner(), HostSystem::MacArm64), InstallResult::Failed);

    for (const InstallResult result : {InstallResult::Installed, InstallResult::Cancelled, InstallResult::Failed}) {
        for (const s2a::Language language : s2a::kLanguages) {
            EXPECT_TRUE(s2a::defaultTranslator().has(s2a::rosetta::messageKey(result), language));
        }
    }
}

} // namespace
