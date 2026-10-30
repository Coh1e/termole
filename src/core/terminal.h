#pragma once

#include <cstdint>
#include <deque>
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
    bool has_selection = false;
    int16_t selection_start_row = 0;
    int16_t selection_start_column = 0;
    int16_t selection_end_row = 0;
    int16_t selection_end_column = 0;
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

    [[nodiscard]] Snapshot snapshot(int scroll_offset = 0) const;
    [[nodiscard]] std::string text() const;
    [[nodiscard]] std::string scrollback_text() const;
    [[nodiscard]] std::string selection_text(int start_row, int start_column, int end_row,
                                             int end_column) const;
    [[nodiscard]] size_t scrollback_line_count() const noexcept { return scrollback_.size(); }
    [[nodiscard]] const std::wstring& title() const noexcept { return title_; }
    [[nodiscard]] uint16_t columns() const noexcept { return columns_; }
    [[nodiscard]] uint16_t rows() const noexcept { return rows_; }
    [[nodiscard]] uint8_t mouse_mode() const noexcept { return mouse_mode_; }
    [[nodiscard]] bool sgr_mouse() const noexcept { return sgr_mouse_; }
    [[nodiscard]] bool bracketed_paste() const noexcept { return bracketed_paste_; }
    [[nodiscard]] bool application_cursor() const noexcept { return application_cursor_; }

    void set_response_handler(std::function<void(std::string_view)> handler)
    {
        respond_ = std::move(handler);
    }

    void set_title_handler(std::function<void(std::wstring_view)> handler)
    {
        on_title_ = std::move(handler);
    }

    void set_bell_handler(std::function<void()> handler)
    {
        on_bell_ = std::move(handler);
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
    void enter_alternate_screen();
    void leave_alternate_screen();
    void set_scroll_region(int top, int bottom);

    void execute_control(char32_t codepoint);
    void print(char32_t codepoint);
    void append_combining(char32_t codepoint);
    void clear_cell(uint16_t column, uint16_t row);
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
    std::deque<std::vector<Cell>> scrollback_;
    std::vector<Cell> primary_cells_;
    bool alternate_ = false;
    uint16_t primary_cursor_column_ = 0;
    uint16_t primary_cursor_row_ = 0;
    bool primary_cursor_visible_ = true;
    uint32_t primary_foreground_ = kColorDefault;
    uint32_t primary_background_ = kColorDefault;
    uint16_t primary_attributes_ = 0;
    uint16_t scroll_top_ = 0;
    uint16_t scroll_bottom_ = 0;
    bool origin_mode_ = false;
    uint8_t mouse_mode_ = 0;
    bool sgr_mouse_ = false;
    bool bracketed_paste_ = false;
    bool application_cursor_ = false;
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
    bool join_next_ = false;

    std::function<void(std::string_view)> respond_;
    std::function<void(std::wstring_view)> on_title_;
    std::function<void()> on_bell_;
};

} // namespace termole
