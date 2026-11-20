#pragma once

#include <termole/core.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace termole {

struct SessionFrame {
    uint64_t revision;
    uint16_t columns;
    uint16_t rows;
    uint16_t cursor_column;
    uint16_t cursor_row;
    bool cursor_visible;
    std::vector<termole_cell> cells;
};

struct FrameSelection {
    std::optional<size_t> anchor;
    size_t focus = 0;
    bool dragging = false;

    void begin(size_t cell)
    {
        anchor = focus = cell;
        dragging = true;
    }

    [[nodiscard]] std::optional<std::pair<size_t, size_t>> range(size_t count) const
    {
        if (!anchor || !count) return std::nullopt;
        const auto first = std::min(*anchor, count - 1);
        const auto last = std::min(focus, count - 1);
        return std::pair{ std::min(first, last), std::max(first, last) };
    }

    [[nodiscard]] std::string text(const SessionFrame& frame) const
    {
        const auto selected = range(frame.cells.size());
        if (!selected) return {};
        std::string text;
        for (size_t index = selected->first; index <= selected->second; ++index) {
            if (index != selected->first && index % frame.columns == 0) text.push_back('\n');
            const auto& cell = frame.cells[index];
            if (cell.continuation) continue;
            if (!cell.codepoint_count) {
                text.push_back(' ');
                continue;
            }
            char utf8[4 * TERMOLE_CELL_MAX_CODEPOINTS + 1];
            size_t bytes = 0;
            if (termole_cell_text(&cell, utf8, sizeof(utf8), &bytes) == TERMOLE_CORE_OK)
                text.append(utf8, bytes);
        }
        return text;
    }
};

} // namespace termole
