#pragma once

namespace termole::app {

// Exit status contract from ADR 0007.
inline constexpr int kExitProgramStartFailed = 1;
inline constexpr int kExitUsage = 2;
inline constexpr int kExitCanceled = 130;
inline constexpr int kExitInfrastructure = 125;

} // namespace termole::app
