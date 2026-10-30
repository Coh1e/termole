#pragma once

#include <cstdint>

namespace termole {

inline constexpr uint16_t kMinColumns = 1;
inline constexpr uint16_t kMaxColumns = 1000;
inline constexpr uint16_t kMinRows = 1;
inline constexpr uint16_t kMaxRows = 1000;
inline constexpr uint32_t kMinFontSizeDip = 8;
inline constexpr uint32_t kMaxFontSizeDip = 96;
inline constexpr size_t kScrollbackLines = 1000;

} // namespace termole
