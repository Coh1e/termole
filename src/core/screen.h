#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace termole {

inline constexpr uint32_t kColorDefault = 0xFFFFFFFFu;
inline constexpr uint32_t kDefaultForeground = 0x00CCCCCCu;
inline constexpr uint32_t kDefaultBackground = 0x000C0C0Cu;

enum CellAttribute : uint16_t {
    kAttrBold = 1u << 0,
    kAttrUnderline = 1u << 1,
    kAttrInverse = 1u << 2,
    kAttrDim = 1u << 3,
    kAttrItalic = 1u << 4,
    kAttrHidden = 1u << 5,
};

struct Cell {
    char32_t character = U' ';
    uint32_t foreground = kColorDefault;
    uint32_t background = kColorDefault;
    uint16_t attributes = 0;
    uint8_t width = 1;
    bool continuation = false;
    std::u32string combining;
};

struct Snapshot {
    uint16_t columns = 0;
    uint16_t rows = 0;
    std::vector<Cell> cells;
    uint16_t cursor_column = 0;
    uint16_t cursor_row = 0;
    bool cursor_visible = true;
    int first_document_row = 0;
    bool live_alternate_screen = false;
    bool has_selection = false;
    int16_t selection_start_row = 0;
    int16_t selection_start_column = 0;
    int16_t selection_end_row = 0;
    int16_t selection_end_column = 0;
};

} // namespace termole
