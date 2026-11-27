#pragma once

#include "core/screen.h"
#include "session/session.h"
#include "view/renderer.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace termole {

// Native child window that presents one NativeSession. The host owns the parent
// window, layout, visibility and focus. All methods are UI-thread only.
class NativeView {
public:
    struct Options {
        std::wstring font_family;
        uint32_t font_size_dip = 0;
        uint32_t theme = 0;
    };

    static Result<std::unique_ptr<NativeView>> create(std::shared_ptr<NativeSession> session, HWND parent);
    static Result<std::unique_ptr<NativeView>> create(std::shared_ptr<NativeSession> session, HWND parent,
                                                const Options& options);

    NativeView(const NativeView&) = delete;
    NativeView& operator=(const NativeView&) = delete;
    ~NativeView();

    [[nodiscard]] HWND hwnd() const noexcept { return window_.load(std::memory_order_acquire); }
    [[nodiscard]] float cell_width() const noexcept { return renderer_ ? renderer_->cell_width() : 0.0f; }
    [[nodiscard]] float cell_height() const noexcept { return renderer_ ? renderer_->cell_height() : 0.0f; }
    [[nodiscard]] uint64_t paint_count() const noexcept { return paint_count_.load(std::memory_order_relaxed); }
    void focus();
    Result<void> set_appearance(const Options& options);

    void scroll(int lines);
    void set_selection(int start_row, int start_column, int end_row, int end_column);
    void clear_selection();
    [[nodiscard]] std::string selection_text() const;
    [[nodiscard]] std::string viewport_text() const;
    Result<void> copy_selection();
    Result<void> paste();

    // Used by the window-class registration; public so the static thunk can
    // be referenced without granting broader access.
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

private:
    NativeView(std::shared_ptr<NativeSession> session, HWND parent);

    Result<void> initialize(const Options& options);
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void on_size();
    void on_paint();
    void paint_client(HWND window, HDC device, bool print = false);
    Snapshot& frame_for_paint(HWND window, Snapshot& latest, bool print);
    void on_character(wchar_t character);
    void on_key_down(WPARAM key);
    void position_ime();
    void on_ime_composition(LPARAM flags);
    void on_mouse_move(int x, int y);
    void on_left_button_down(int x, int y, bool shift);
    void on_left_button_up(int x, int y);
    void on_mouse_wheel(int delta, bool shift, int x, int y);
    [[nodiscard]] bool mouse_coordinates(int x, int y, int& row, int& column) const;
    [[nodiscard]] bool mouse_screen_coordinates(int x, int y, int& row, int& column) const;
    void report_mouse(int button, int x, int y, bool press);
    [[nodiscard]] bool selected_cell(int row, int column) const;
    Result<void> send_text(std::string_view bytes);
    Result<void> start_watcher();
    void stop_watcher();

    std::shared_ptr<NativeSession> session_;
    HWND parent_ = nullptr;
    std::atomic<HWND> window_{ nullptr };
    std::unique_ptr<Renderer> renderer_;
    Snapshot last_visible_cursor_frame_;
    ULONGLONG redraw_deadline_ = 0;
    size_t remaining_glyphs_ = 0;
    Appearance appearance_;
    std::thread watcher_;
    HANDLE stop_event_ = nullptr;
    bool focused_ = false;
    bool shift_down_ = false;
    int mouse_modifiers_ = 0;
    bool attached_ = false;
    int scroll_offset_ = 0;
    bool has_selection_ = false;
    bool selecting_ = false;
    bool mouse_reporting_ = false;
    int mouse_button_ = 0;
    int selection_anchor_row_ = 0;
    int selection_anchor_column_ = 0;
    int selection_focus_row_ = 0;
    int selection_focus_column_ = 0;
    wchar_t high_surrogate_ = 0;
    uint16_t last_columns_ = 0;
    uint16_t last_rows_ = 0;
    std::atomic<uint64_t> paint_count_{ 0 };
};

} // namespace termole
