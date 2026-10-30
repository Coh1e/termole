#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
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
};

struct Snapshot {
    uint16_t columns = 0;
    uint16_t rows = 0;
    std::vector<Cell> cells;
    uint16_t cursor_column = 0;
    uint16_t cursor_row = 0;
    bool cursor_visible = true;
};

// Minimal VT screen state for this slice: text, cursor addressing, erase,
// SGR colours, scrolling and window title. Alternate screen, scrollback,
// mouse modes, bracketed paste, scroll regions and wide/combining cell layout
// are deliberately absent until their acceptance slices (see docs/vt-core.md).
class Terminal {
public:
    Terminal(uint16_t columns, uint16_t rows);

    void reset();
    void resize(uint16_t columns, uint16_t rows);
    void input(char32_t codepoint);

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::string text() const;
    [[nodiscard]] const std::wstring& title() const noexcept { return title_; }
    [[nodiscard]] uint16_t columns() const noexcept { return columns_; }
    [[nodiscard]] uint16_t rows() const noexcept { return rows_; }

    void set_response_handler(std::function<void(std::string_view)> handler)
    {
        respond_ = std::move(handler);
    }

private:
    enum class State {
        Ground,
        Escape,
        EscapeIntermediate,
        CsiParam,
        CsiIgnore,
        OscString,
        OscEscape,
        IgnoreString,
        IgnoreStringEscape,
    };

    [[nodiscard]] Cell blank() const;
    [[nodiscard]] Cell& at(uint16_t column, uint16_t row);
    [[nodiscard]] const Cell& at(uint16_t column, uint16_t row) const;

    void handle_escape(char32_t codepoint);
    void handle_csi(char32_t codepoint);
    void dispatch_csi(char32_t final);
    void dispatch_osc();

    void execute_control(char32_t codepoint);
    void print(char32_t codepoint);
    void linefeed();
    void reverse_index();
    void scroll_up(int lines);
    void scroll_down(int lines);
    void erase_in_display(int mode);
    void erase_in_line(int mode);
    void insert_lines(int count);
    void delete_lines(int count);
    void insert_chars(int count);
    void delete_chars(int count);
    void erase_chars(int count);
    void apply_sgr();
    void set_mode(bool enabled);
    void save_cursor();
    void restore_cursor();

    [[nodiscard]] int param(size_t index, int fallback) const;
    void respond(std::string_view text);
    void push_param();
    void start_csi();

    uint16_t columns_;
    uint16_t rows_;
    std::vector<Cell> cells_;
    uint16_t cursor_column_ = 0;
    uint16_t cursor_row_ = 0;
    bool cursor_visible_ = true;
    bool auto_wrap_ = true;
    bool wrap_pending_ = false;

    uint32_t foreground_ = kColorDefault;
    uint32_t background_ = kColorDefault;
    uint16_t attributes_ = 0;

    uint16_t saved_column_ = 0;
    uint16_t saved_row_ = 0;
    uint32_t saved_foreground_ = kColorDefault;
    uint32_t saved_background_ = kColorDefault;
    uint16_t saved_attributes_ = 0;

    State state_ = State::Ground;
    std::vector<int> params_;
    int current_param_ = 0;
    bool have_current_param_ = false;
    char32_t private_marker_ = 0;
    std::u32string osc_payload_;
    std::wstring title_;
    char32_t last_printed_ = U' ';

    std::function<void(std::string_view)> respond_;
};

} // namespace termole
