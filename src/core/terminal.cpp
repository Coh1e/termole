#include "core/terminal.h"

#include "core/utf8.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace termole {
namespace {

constexpr std::array<uint32_t, 16> kSystemPalette{
    0x000C0C0C, 0x00C50F1F, 0x0013A10E, 0x00C19C00, 0x000037DA, 0x00881798, 0x003A96DD, 0x00CCCCCC,
    0x00767676, 0x00E74856, 0x0016C60C, 0x00F9F1A5, 0x003B78FF, 0x00B4009E, 0x0061D6D6, 0x00F2F2F2,
};

uint32_t palette_color(int index)
{
    if (index < 0) index = 0;
    if (index < 16) return kSystemPalette[static_cast<size_t>(index)];
    if (index < 232) {
        const int value = index - 16;
        const int red = value / 36;
        const int green = (value / 6) % 6;
        const int blue = value % 6;
        const auto component = [](int level) { return level == 0 ? 0 : 55 + level * 40; };
        return (static_cast<uint32_t>(component(red)) << 16) |
            (static_cast<uint32_t>(component(green)) << 8) | static_cast<uint32_t>(component(blue));
    }
    if (index < 256) {
        const int level = 8 + (index - 232) * 10;
        return (static_cast<uint32_t>(level) << 16) | (static_cast<uint32_t>(level) << 8) |
            static_cast<uint32_t>(level);
    }
    return 0x00FFFFFF;
}

uint32_t rgb_color(int red, int green, int blue)
{
    return (static_cast<uint32_t>(std::clamp(red, 0, 255)) << 16) |
        (static_cast<uint32_t>(std::clamp(green, 0, 255)) << 8) |
        static_cast<uint32_t>(std::clamp(blue, 0, 255));
}

} // namespace

Terminal::Terminal(uint16_t columns, uint16_t rows)
    : columns_(columns == 0 ? 80 : columns), rows_(rows == 0 ? 24 : rows),
      cells_(static_cast<size_t>(columns_) * rows_, Cell{})
{
}

Cell Terminal::blank() const
{
    Cell cell;
    cell.background = background_;
    return cell;
}

Cell& Terminal::at(uint16_t column, uint16_t row)
{
    return cells_[static_cast<size_t>(row) * columns_ + column];
}

const Cell& Terminal::at(uint16_t column, uint16_t row) const
{
    return cells_[static_cast<size_t>(row) * columns_ + column];
}

void Terminal::reset()
{
    cells_.assign(static_cast<size_t>(columns_) * rows_, Cell{});
    cursor_column_ = 0;
    cursor_row_ = 0;
    cursor_visible_ = true;
    auto_wrap_ = true;
    wrap_pending_ = false;
    foreground_ = kColorDefault;
    background_ = kColorDefault;
    attributes_ = 0;
    saved_column_ = 0;
    saved_row_ = 0;
    saved_foreground_ = kColorDefault;
    saved_background_ = kColorDefault;
    saved_attributes_ = 0;
    state_ = State::Ground;
    params_.clear();
    current_param_ = 0;
    have_current_param_ = false;
    private_marker_ = 0;
    osc_payload_.clear();
    title_.clear();
    last_printed_ = U' ';
}

void Terminal::resize(uint16_t columns, uint16_t rows)
{
    if (columns == 0 || rows == 0 || (columns == columns_ && rows == rows_)) return;

    std::vector<Cell> replacement(static_cast<size_t>(columns) * rows, Cell{});
    const uint16_t copy_columns = std::min(columns, columns_);
    const uint16_t copy_rows = std::min(rows, rows_);
    for (uint16_t row = 0; row < copy_rows; ++row) {
        for (uint16_t column = 0; column < copy_columns; ++column) {
            replacement[static_cast<size_t>(row) * columns + column] = at(column, row);
        }
    }

    cells_ = std::move(replacement);
    columns_ = columns;
    rows_ = rows;
    cursor_column_ = std::min(cursor_column_, static_cast<uint16_t>(columns_ - 1));
    cursor_row_ = std::min(cursor_row_, static_cast<uint16_t>(rows_ - 1));
    wrap_pending_ = false;
}

void Terminal::input(char32_t codepoint)
{
    switch (state_) {
    case State::Ground:
        if (codepoint == 0x1B) {
            state_ = State::Escape;
            return;
        }
        if (codepoint == 0x9B) {
            start_csi();
            return;
        }
        if (codepoint == 0x9D) {
            state_ = State::OscString;
            osc_payload_.clear();
            return;
        }
        if (codepoint == 0x90 || codepoint == 0x98 || codepoint == 0x9E || codepoint == 0x9F) {
            state_ = State::IgnoreString;
            return;
        }
        if (codepoint == 0x9C) return;
        if (codepoint < 0x20 || codepoint == 0x7F) {
            execute_control(codepoint);
            return;
        }
        if (codepoint >= 0x80 && codepoint <= 0x9F) {
            execute_control(codepoint);
            return;
        }
        print(codepoint);
        return;

    case State::Escape:
        handle_escape(codepoint);
        return;

    case State::EscapeIntermediate:
        state_ = codepoint == 0x1B ? State::Escape : State::Ground;
        return;

    case State::CsiParam:
        handle_csi(codepoint);
        return;

    case State::CsiIgnore:
        if (codepoint == 0x1B) {
            state_ = State::Escape;
        } else if (codepoint >= 0x40 && codepoint <= 0x7E) {
            state_ = State::Ground;
        }
        return;

    case State::OscString:
        if (codepoint == 0x07 || codepoint == 0x9C) {
            dispatch_osc();
            state_ = State::Ground;
        } else if (codepoint == 0x1B) {
            state_ = State::OscEscape;
        } else if (codepoint >= 0x20 && osc_payload_.size() < 512) {
            osc_payload_.push_back(codepoint);
        }
        return;

    case State::OscEscape:
        if (codepoint == U'\\' || codepoint == 0x9C) {
            dispatch_osc();
            state_ = State::Ground;
        } else {
            state_ = State::OscString;
            if (codepoint >= 0x20 && osc_payload_.size() < 512) osc_payload_.push_back(codepoint);
        }
        return;

    case State::IgnoreString:
        if (codepoint == 0x1B) state_ = State::IgnoreStringEscape;
        else if (codepoint == 0x9C) state_ = State::Ground;
        return;

    case State::IgnoreStringEscape:
        state_ = codepoint == U'\\' ? State::Ground : State::IgnoreString;
        return;
    }
}

void Terminal::start_csi()
{
    state_ = State::CsiParam;
    params_.clear();
    current_param_ = 0;
    have_current_param_ = false;
    private_marker_ = 0;
}

void Terminal::push_param()
{
    if (params_.size() < 32) {
        params_.push_back(have_current_param_ ? current_param_ : -1);
    }
    current_param_ = 0;
    have_current_param_ = false;
}

void Terminal::handle_escape(char32_t codepoint)
{
    if (codepoint == 0x1B) {
        state_ = State::Escape;
        return;
    }
    if (codepoint == U'[') {
        start_csi();
        return;
    }
    if (codepoint == U']') {
        state_ = State::OscString;
        osc_payload_.clear();
        return;
    }
    if (codepoint == U'P' || codepoint == U'X' || codepoint == U'^' || codepoint == U'_') {
        state_ = State::IgnoreString;
        return;
    }
    if (codepoint == U'(' || codepoint == U')' || codepoint == U'*' || codepoint == U'+' ||
        codepoint == U'#' || codepoint == U'%' || codepoint == U' ') {
        state_ = State::EscapeIntermediate;
        return;
    }

    state_ = State::Ground;
    switch (codepoint) {
    case U'7':
        save_cursor();
        break;
    case U'8':
        restore_cursor();
        break;
    case U'D':
        linefeed();
        break;
    case U'E':
        cursor_column_ = 0;
        linefeed();
        break;
    case U'M':
        reverse_index();
        break;
    case U'c':
        reset();
        break;
    case U'\\':
        break;
    default:
        break;
    }
}

void Terminal::handle_csi(char32_t codepoint)
{
    if (codepoint == 0x1B) {
        state_ = State::Escape;
        return;
    }
    if (codepoint >= U'0' && codepoint <= U'9') {
        current_param_ = std::min(9999, current_param_ * 10 + static_cast<int>(codepoint - U'0'));
        have_current_param_ = true;
        return;
    }
    if (codepoint == U';' || codepoint == U':') {
        push_param();
        return;
    }
    if (codepoint == U'?' || codepoint == U'>' || codepoint == U'<' || codepoint == U'=') {
        if (params_.empty() && !have_current_param_) {
            private_marker_ = codepoint;
            return;
        }
        state_ = State::CsiIgnore;
        return;
    }
    if (codepoint >= 0x40 && codepoint <= 0x7E) {
        if (have_current_param_) push_param();
        dispatch_csi(codepoint);
        state_ = State::Ground;
        return;
    }
    if (codepoint >= 0x20 && codepoint <= 0x2F) {
        if (have_current_param_) push_param();
        state_ = State::CsiIgnore;
        return;
    }
    // Other C0 controls inside a CSI sequence are ignored for this slice.
}

int Terminal::param(size_t index, int fallback) const
{
    if (index >= params_.size()) return fallback;
    const int value = params_[index];
    return value < 0 ? fallback : value;
}

void Terminal::dispatch_csi(char32_t final)
{
    switch (final) {
    case U'@':
        insert_chars(std::max(1, param(0, 1)));
        break;
    case U'A':
        cursor_row_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_row_) - std::max(1, param(0, 1)), 0, rows_ - 1));
        wrap_pending_ = false;
        break;
    case U'B':
        cursor_row_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_row_) + std::max(1, param(0, 1)), 0, rows_ - 1));
        wrap_pending_ = false;
        break;
    case U'C':
        cursor_column_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_column_) + std::max(1, param(0, 1)), 0, columns_ - 1));
        wrap_pending_ = false;
        break;
    case U'D':
        cursor_column_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_column_) - std::max(1, param(0, 1)), 0, columns_ - 1));
        wrap_pending_ = false;
        break;
    case U'E':
        cursor_row_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_row_) + std::max(1, param(0, 1)), 0, rows_ - 1));
        cursor_column_ = 0;
        wrap_pending_ = false;
        break;
    case U'F':
        cursor_row_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_row_) - std::max(1, param(0, 1)), 0, rows_ - 1));
        cursor_column_ = 0;
        wrap_pending_ = false;
        break;
    case U'G':
        cursor_column_ = static_cast<uint16_t>(std::clamp(param(0, 1) - 1, 0, columns_ - 1));
        wrap_pending_ = false;
        break;
    case U'H':
    case U'f':
        cursor_row_ = static_cast<uint16_t>(std::clamp(param(0, 1) - 1, 0, rows_ - 1));
        cursor_column_ = static_cast<uint16_t>(std::clamp(param(1, 1) - 1, 0, columns_ - 1));
        wrap_pending_ = false;
        break;
    case U'J':
        erase_in_display(param(0, 0));
        break;
    case U'K':
        erase_in_line(param(0, 0));
        break;
    case U'L':
        insert_lines(std::max(1, param(0, 1)));
        break;
    case U'M':
        delete_lines(std::max(1, param(0, 1)));
        break;
    case U'P':
        delete_chars(std::max(1, param(0, 1)));
        break;
    case U'S':
        scroll_up(std::max(1, param(0, 1)));
        break;
    case U'T':
        scroll_down(std::max(1, param(0, 1)));
        break;
    case U'X':
        erase_chars(std::max(1, param(0, 1)));
        break;
    case U'a':
        cursor_column_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_column_) + std::max(1, param(0, 1)), 0, columns_ - 1));
        wrap_pending_ = false;
        break;
    case U'b':
        for (int index = 0; index < std::max(1, param(0, 1)); ++index) print(last_printed_);
        break;
    case U'c':
        if (private_marker_ == 0) respond("\x1b[?1;2c");
        break;
    case U'd':
        cursor_row_ = static_cast<uint16_t>(std::clamp(param(0, 1) - 1, 0, rows_ - 1));
        wrap_pending_ = false;
        break;
    case U'e':
        cursor_row_ = static_cast<uint16_t>(
            std::clamp(static_cast<int>(cursor_row_) + std::max(1, param(0, 1)), 0, rows_ - 1));
        wrap_pending_ = false;
        break;
    case U'h':
    case U'l':
        set_mode(final == U'h');
        break;
    case U'I':
        for (int index = 0; index < std::max(1, param(0, 1)); ++index) {
            const int next = (static_cast<int>(cursor_column_) / 8 + 1) * 8;
            cursor_column_ = static_cast<uint16_t>(std::min(next, columns_ - 1));
        }
        wrap_pending_ = false;
        break;
    case U'm':
        apply_sgr();
        break;
    case U'n':
        if (param(0, 0) == 5) {
            respond("\x1b[0n");
        } else if (param(0, 0) == 6) {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "\x1b[%d;%dR", cursor_row_ + 1, cursor_column_ + 1);
            respond(buffer);
        }
        break;
    case U's':
        save_cursor();
        break;
    case U'u':
        restore_cursor();
        break;
    case U'Z':
        for (int index = 0; index < std::max(1, param(0, 1)); ++index) {
            const int previous = ((static_cast<int>(cursor_column_) - 1) / 8) * 8;
            cursor_column_ = static_cast<uint16_t>(std::max(0, previous));
        }
        wrap_pending_ = false;
        break;
    default:
        break;
    }
}

void Terminal::dispatch_osc()
{
    if (!osc_payload_.empty()) {
        size_t separator = 0;
        while (separator < osc_payload_.size() && osc_payload_[separator] >= U'0' && osc_payload_[separator] <= U'9')
            ++separator;
        if (separator > 0 && separator < osc_payload_.size() && osc_payload_[separator] == U';') {
            int code = 0;
            for (size_t index = 0; index < separator; ++index)
                code = code * 10 + static_cast<int>(osc_payload_[index] - U'0');
            if (code == 0 || code == 2) {
                std::wstring title;
                title.reserve(osc_payload_.size() - separator - 1);
                for (size_t index = separator + 1; index < osc_payload_.size(); ++index) {
                    const char32_t codepoint = osc_payload_[index];
                    if (codepoint <= 0xFFFF) {
                        title.push_back(static_cast<wchar_t>(codepoint));
                    } else if (codepoint <= 0x10FFFF) {
                        const char32_t value = codepoint - 0x10000;
                        title.push_back(static_cast<wchar_t>(0xD800 + (value >> 10)));
                        title.push_back(static_cast<wchar_t>(0xDC00 + (value & 0x3FF)));
                    }
                }
                if (title_ != title) {
                    title_ = std::move(title);
                }
            }
        }
    }
    osc_payload_.clear();
}

void Terminal::execute_control(char32_t codepoint)
{
    switch (codepoint) {
    case 0x08: // BS
        wrap_pending_ = false;
        if (cursor_column_ > 0) --cursor_column_;
        break;
    case 0x09: { // HT
        wrap_pending_ = false;
        const int next = (static_cast<int>(cursor_column_) / 8 + 1) * 8;
        cursor_column_ = static_cast<uint16_t>(std::min(next, static_cast<int>(columns_) - 1));
        break;
    }
    case 0x0A: // LF
    case 0x0B: // VT
    case 0x0C: // FF
        linefeed();
        break;
    case 0x0D: // CR
        cursor_column_ = 0;
        wrap_pending_ = false;
        break;
    case 0x84: // IND (C1)
        linefeed();
        break;
    case 0x85: // NEL (C1)
        cursor_column_ = 0;
        linefeed();
        break;
    case 0x8D: // RI (C1)
        reverse_index();
        break;
    default:
        break;
    }
}

void Terminal::print(char32_t codepoint)
{
    if (codepoint == 0) return;

    if (wrap_pending_) {
        if (auto_wrap_) {
            cursor_column_ = 0;
            linefeed();
        }
        wrap_pending_ = false;
    }

    if (cursor_column_ >= columns_) cursor_column_ = static_cast<uint16_t>(columns_ - 1);
    Cell& cell = at(cursor_column_, cursor_row_);
    cell = Cell{ codepoint, foreground_, background_, attributes_ };
    last_printed_ = codepoint;

    if (cursor_column_ + 1 >= columns_) {
        if (auto_wrap_) {
            wrap_pending_ = true;
            cursor_column_ = static_cast<uint16_t>(columns_ - 1);
        }
    } else {
        ++cursor_column_;
    }
}

void Terminal::linefeed()
{
    wrap_pending_ = false;
    if (cursor_row_ + 1 < rows_) {
        ++cursor_row_;
    } else {
        scroll_up(1);
    }
}

void Terminal::reverse_index()
{
    wrap_pending_ = false;
    if (cursor_row_ > 0) {
        --cursor_row_;
    } else {
        scroll_down(1);
    }
}

void Terminal::scroll_up(int lines)
{
    if (lines <= 0) return;
    if (lines >= rows_) {
        for (auto& cell : cells_) cell = Cell{};
    } else {
        const size_t stride = columns_;
        std::move(cells_.begin() + static_cast<std::ptrdiff_t>(lines * stride), cells_.end(),
                  cells_.begin());
        for (size_t index = cells_.size() - static_cast<size_t>(lines) * stride; index < cells_.size();
             ++index)
            cells_[index] = blank();
    }
}

void Terminal::scroll_down(int lines)
{
    if (lines <= 0) return;
    if (lines >= rows_) {
        for (auto& cell : cells_) cell = Cell{};
    } else {
        const size_t stride = columns_;
        std::move_backward(cells_.begin(), cells_.end() - static_cast<std::ptrdiff_t>(lines * stride),
                           cells_.end());
        for (size_t index = 0; index < static_cast<size_t>(lines) * stride; ++index) cells_[index] = blank();
    }
}

void Terminal::erase_in_display(int mode)
{
    if (mode == 0) {
        for (uint16_t column = cursor_column_; column < columns_; ++column) at(column, cursor_row_) = blank();
        for (uint16_t row = static_cast<uint16_t>(cursor_row_ + 1); row < rows_; ++row)
            for (uint16_t column = 0; column < columns_; ++column) at(column, row) = blank();
    } else if (mode == 1) {
        for (uint16_t row = 0; row < cursor_row_; ++row)
            for (uint16_t column = 0; column < columns_; ++column) at(column, row) = blank();
        for (uint16_t column = 0; column <= cursor_column_ && column < columns_; ++column)
            at(column, cursor_row_) = blank();
    } else {
        for (auto& cell : cells_) cell = blank();
    }
    wrap_pending_ = false;
}

void Terminal::erase_in_line(int mode)
{
    if (mode == 0) {
        for (uint16_t column = cursor_column_; column < columns_; ++column) at(column, cursor_row_) = blank();
    } else if (mode == 1) {
        for (uint16_t column = 0; column <= cursor_column_ && column < columns_; ++column)
            at(column, cursor_row_) = blank();
    } else {
        for (uint16_t column = 0; column < columns_; ++column) at(column, cursor_row_) = blank();
    }
    wrap_pending_ = false;
}

void Terminal::insert_lines(int count)
{
    if (count <= 0 || cursor_row_ >= rows_) return;
    count = std::min<int>(count, rows_ - cursor_row_);
    const size_t stride = columns_;
    const size_t first = static_cast<size_t>(cursor_row_) * stride;
    std::move_backward(cells_.begin() + static_cast<std::ptrdiff_t>(first),
                       cells_.end() - static_cast<std::ptrdiff_t>(count * stride), cells_.end());
    for (size_t index = first; index < first + static_cast<size_t>(count) * stride; ++index)
        cells_[index] = blank();
}

void Terminal::delete_lines(int count)
{
    if (count <= 0 || cursor_row_ >= rows_) return;
    count = std::min<int>(count, rows_ - cursor_row_);
    const size_t stride = columns_;
    const size_t first = static_cast<size_t>(cursor_row_) * stride;
    std::move(cells_.begin() + static_cast<std::ptrdiff_t>(first + count * stride), cells_.end(),
              cells_.begin() + static_cast<std::ptrdiff_t>(first));
    for (size_t index = cells_.size() - static_cast<size_t>(count) * stride; index < cells_.size();
         ++index)
        cells_[index] = blank();
}

void Terminal::insert_chars(int count)
{
    if (count <= 0) return;
    count = std::min<int>(count, columns_ - cursor_column_);
    for (uint16_t column = static_cast<uint16_t>(columns_ - 1); column >= cursor_column_ + count;
         --column)
        at(column, cursor_row_) = at(static_cast<uint16_t>(column - count), cursor_row_);
    for (uint16_t column = cursor_column_; column < cursor_column_ + count; ++column)
        at(column, cursor_row_) = blank();
}

void Terminal::delete_chars(int count)
{
    if (count <= 0) return;
    count = std::min<int>(count, columns_ - cursor_column_);
    for (uint16_t column = cursor_column_; column + count < columns_; ++column)
        at(column, cursor_row_) = at(static_cast<uint16_t>(column + count), cursor_row_);
    for (uint16_t column = static_cast<uint16_t>(columns_ - count); column < columns_; ++column)
        at(column, cursor_row_) = blank();
}

void Terminal::erase_chars(int count)
{
    if (count <= 0) return;
    const int end = std::min<int>(columns_, cursor_column_ + count);
    for (int column = cursor_column_; column < end; ++column)
        at(static_cast<uint16_t>(column), cursor_row_) = blank();
}

void Terminal::apply_sgr()
{
    if (params_.empty()) {
        foreground_ = kColorDefault;
        background_ = kColorDefault;
        attributes_ = 0;
        return;
    }

    for (size_t index = 0; index < params_.size(); ++index) {
        const int value = params_[index] < 0 ? 0 : params_[index];
        if (value == 0) {
            foreground_ = kColorDefault;
            background_ = kColorDefault;
            attributes_ = 0;
        } else if (value == 1) {
            attributes_ |= kAttrBold;
        } else if (value == 2) {
            attributes_ |= kAttrDim;
        } else if (value == 3) {
            attributes_ |= kAttrItalic;
        } else if (value == 4 || value == 21) {
            attributes_ |= kAttrUnderline;
        } else if (value == 7) {
            attributes_ |= kAttrInverse;
        } else if (value == 8) {
            attributes_ |= kAttrHidden;
        } else if (value == 22) {
            attributes_ &= static_cast<uint16_t>(~(kAttrBold | kAttrDim));
        } else if (value == 23) {
            attributes_ &= static_cast<uint16_t>(~kAttrItalic);
        } else if (value == 24) {
            attributes_ &= static_cast<uint16_t>(~kAttrUnderline);
        } else if (value == 27) {
            attributes_ &= static_cast<uint16_t>(~kAttrInverse);
        } else if (value == 28) {
            attributes_ &= static_cast<uint16_t>(~kAttrHidden);
        } else if (value >= 30 && value <= 37) {
            foreground_ = palette_color(value - 30);
        } else if (value == 39) {
            foreground_ = kColorDefault;
        } else if (value >= 40 && value <= 47) {
            background_ = palette_color(value - 40);
        } else if (value == 49) {
            background_ = kColorDefault;
        } else if (value >= 90 && value <= 97) {
            foreground_ = palette_color(8 + value - 90);
        } else if (value >= 100 && value <= 107) {
            background_ = palette_color(8 + value - 100);
        } else if (value == 38 || value == 48) {
            const bool is_foreground = value == 38;
            if (index + 1 < params_.size()) {
                const int mode = params_[index + 1];
                if (mode == 5 && index + 2 < params_.size()) {
                    const int palette = params_[index + 2];
                    index += 2;
                    if (is_foreground) foreground_ = palette_color(palette);
                    else background_ = palette_color(palette);
                } else if (mode == 2 && index + 4 < params_.size()) {
                    const int red = params_[index + 2];
                    const int green = params_[index + 3];
                    const int blue = params_[index + 4];
                    index += 4;
                    if (is_foreground) foreground_ = rgb_color(red, green, blue);
                    else background_ = rgb_color(red, green, blue);
                }
            }
        }
    }
}

void Terminal::set_mode(bool enabled)
{
    for (const int raw : params_) {
        const int mode = raw < 0 ? 0 : raw;
        if (private_marker_ == U'?') {
            if (mode == 25) {
                cursor_visible_ = enabled;
            } else if (mode == 7) {
                auto_wrap_ = enabled;
                if (!enabled) wrap_pending_ = false;
            }
        }
    }
}

void Terminal::save_cursor()
{
    saved_column_ = cursor_column_;
    saved_row_ = cursor_row_;
    saved_foreground_ = foreground_;
    saved_background_ = background_;
    saved_attributes_ = attributes_;
}

void Terminal::restore_cursor()
{
    cursor_column_ = std::min(saved_column_, static_cast<uint16_t>(columns_ - 1));
    cursor_row_ = std::min(saved_row_, static_cast<uint16_t>(rows_ - 1));
    foreground_ = saved_foreground_;
    background_ = saved_background_;
    attributes_ = saved_attributes_;
    wrap_pending_ = false;
}

void Terminal::respond(std::string_view text)
{
    if (respond_) respond_(text);
}

Snapshot Terminal::snapshot() const
{
    Snapshot result;
    result.columns = columns_;
    result.rows = rows_;
    result.cells = cells_;
    result.cursor_column = cursor_column_;
    result.cursor_row = cursor_row_;
    result.cursor_visible = cursor_visible_;
    return result;
}

std::string Terminal::text() const
{
    std::vector<std::string> rows;
    rows.reserve(rows_);
    int last_content_row = -1;

    for (uint16_t row = 0; row < rows_; ++row) {
        std::string line;
        line.reserve(columns_);
        for (uint16_t column = 0; column < columns_; ++column) {
            const Cell& cell = at(column, row);
            append_utf8(line, cell.character == 0 ? U' ' : cell.character);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (!line.empty()) last_content_row = static_cast<int>(row);
        rows.push_back(std::move(line));
    }

    if (last_content_row < 0) return {};

    std::string result;
    for (int row = 0; row <= last_content_row; ++row) {
        result += rows[static_cast<size_t>(row)];
        if (row != last_content_row) result.push_back('\n');
    }
    return result;
}

} // namespace termole
