#include "s2a/rosetta.hpp"

#include <chrono>
#include <string>

namespace s2a::rosetta {

bool missing(const process::Runner& runner, toolchain::HostSystem host) {
    if (host != toolchain::HostSystem::MacArm64) {
        return false;
    }
    // "arch -x86_64" fails ("Bad CPU type in executable") when Rosetta is not installed.
    process::Command command;
    command.program = "/usr/bin/arch";
    command.args = {"-x86_64", "/usr/bin/true"};
    command.timeout = std::chrono::seconds(10);
    const auto result = runner(command);
    return !result.ok() || !result.value().succeeded();
}

InstallResult install(const process::Runner& runner, toolchain::HostSystem host) {
    if (!missing(runner, host)) {
        return InstallResult::Installed;
    }
    // osascript shows the administrator password prompt of macOS (no Terminal, no sudo).
    process::Command command;
    command.program = "/usr/bin/osascript";
    command.args = {"-e", "do shell script \"/usr/sbin/" + std::string(kInstallCommand) +
                              "\" with administrator privileges"};
    command.timeout = std::chrono::minutes(20);
    const auto result = runner(command);
    if (!result.ok()) {
        return InstallResult::Failed;
    }
    const auto& output = result.value();
    // "execution error: User canceled. (-128)"
    if (!output.succeeded() && output.err.find("-128") != std::string::npos) {
        return InstallResult::Cancelled;
    }
    if (!output.succeeded() || missing(runner, host)) {
        return InstallResult::Failed;
    }
    return InstallResult::Installed;
}

std::string_view messageKey(InstallResult result) noexcept {
    switch (result) {
    case InstallResult::Installed:
        return "rosetta.installed";
    case InstallResult::Cancelled:
        return "rosetta.cancelled";
    case InstallResult::Failed:
        return "rosetta.failed";
    }
    return "rosetta.failed";
}

} // namespace s2a::rosetta
