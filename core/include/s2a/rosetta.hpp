#pragma once

#include "s2a/board_packages.hpp"
#include "s2a/process.hpp"

#include <string_view>

/// Rosetta 2 on Macs with an Apple chip (M1 to M4): the compilers of most boards (avr-gcc for the
/// Arduino AVR boards, arm-none-eabi-gcc for the Uno R4...) only exist for Intel Macs, and macOS
/// runs them through Rosetta, which is not installed by default.
namespace s2a::rosetta {

/// True when Rosetta must be there and is not: a Mac with an Apple chip that cannot run an Intel
/// program. Always false on other computers.
[[nodiscard]] bool missing(const process::Runner& runner = process::systemRunner(),
                           toolchain::HostSystem host = toolchain::currentHost());

enum class InstallResult {
    Installed,
    Cancelled, ///< The user closed the password prompt of macOS.
    Failed,    ///< No Internet, or macOS refused (the details are in the log).
};

/// Installs Rosetta with Apple's tool (softwareupdate), behind the administrator password prompt
/// of macOS. Downloaded from Apple: Internet needed, a few minutes at most.
[[nodiscard]] InstallResult install(const process::Runner& runner = process::systemRunner(),
                                    toolchain::HostSystem host = toolchain::currentHost());

/// Translation key of the result ("rosetta.installed"...).
[[nodiscard]] std::string_view messageKey(InstallResult result) noexcept;

/// The Terminal command that installs Rosetta (s2a-cli prints it).
inline constexpr std::string_view kInstallCommand = "softwareupdate --install-rosetta --agree-to-license";

} // namespace s2a::rosetta
