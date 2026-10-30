#pragma once

#include "core/terminal.h"
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

// Native child window that presents one Session. The host owns the parent
// window, layout, visibility and focus. All methods are UI-thread only.
class View {
public:
    static Result<std::unique_ptr<View>> create(std::shared_ptr<Session> session, HWND parent,
                                                uint32_t font_size_dip);

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    ~View();

    [[nodiscard]] HWND hwnd() const noexcept { return window_.load(std::memory_order_acquire); }
    void focus();

    // Used by the window-class registration; public so the static thunk can
    // be referenced without granting broader access.
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

private:
    View(std::shared_ptr<Session> session, HWND parent);

    Result<void> initialize(uint32_t font_size_dip);
    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
    void on_size();
    void on_paint();
    void on_character(wchar_t character);
    void on_key_down(WPARAM key);
    Result<void> send_text(std::string_view bytes);
    Result<void> start_watcher();
    void stop_watcher();

    std::shared_ptr<Session> session_;
    HWND parent_ = nullptr;
    std::atomic<HWND> window_{ nullptr };
    std::unique_ptr<Renderer> renderer_;
    std::thread watcher_;
    HANDLE stop_event_ = nullptr;
    bool focused_ = false;
    bool attached_ = false;
    wchar_t high_surrogate_ = 0;
    uint16_t last_columns_ = 0;
    uint16_t last_rows_ = 0;
};

} // namespace termole
