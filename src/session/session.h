#pragma once

#include "common/error.h"
#include "common/limits.h"
#include "common/win.h"
#include "core/frame.h"
#include "core/screen.h"
#include "platform/windows_pty.h"

#include <termole/core.h>
#include <termole/termole.h>

#include <atomic>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace termole {

using SessionOptions = platform::WindowsPtyOptions;

// Owns the ConPTY, the managed process tree and the terminal screen. It does
// not create a top-level window and can continue consuming output with no NativeView
// attached.
class NativeSession {
public:
    static Result<std::shared_ptr<NativeSession>> create(const SessionOptions& options);

    NativeSession(const NativeSession&) = delete;
    NativeSession& operator=(const NativeSession&) = delete;

    ~NativeSession();

    Result<void> resize(uint16_t columns, uint16_t rows);
    Result<void> write(std::string_view bytes);
    Result<void> send_key(termole_key key);
    Result<void> terminate();

    [[nodiscard]] Snapshot snapshot(int scroll_offset = 0) const;
    [[nodiscard]] std::optional<SessionFrame> frame_after(uint64_t revision) const;
    [[nodiscard]] termole_modes modes() const;
    [[nodiscard]] std::pair<uint16_t, uint16_t> size() const;
    [[nodiscard]] std::string screen_text() const;
    [[nodiscard]] std::string selection_text(int start_row, int start_column, int end_row,
                                             int end_column) const;
    [[nodiscard]] std::string scrollback_text() const;
    [[nodiscard]] size_t scrollback_line_count() const;
    [[nodiscard]] std::wstring title() const;
    [[nodiscard]] uint8_t mouse_mode() const;
    [[nodiscard]] bool sgr_mouse() const;
    [[nodiscard]] bool bracketed_paste() const;
    [[nodiscard]] bool application_cursor() const;
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] HANDLE data_event() const noexcept { return data_event_.get(); }
    Result<int32_t> exit_code() const;

    struct EventRecord {
        termole_event_type type = TERMOLE_EVENT_NONE;
        int32_t exit_code = 0;
        std::string text;
    };

    // Pops one queued event. Returns false when the queue is empty.
    bool poll_event(EventRecord& out);

    // The NativeSession owns at most one NativeView, as required by the accepted design.
    [[nodiscard]] bool attach_view() noexcept;
    void detach_view() noexcept;

private:
    explicit NativeSession(uint16_t columns, uint16_t rows);

    void read_loop();
    static void on_write_pty(void* userdata, const void* data, size_t size);
    static void on_bell(void* userdata);
    static void on_title_changed(void* userdata);
    void exit_loop();
    Result<void> write_raw(std::string_view bytes);
    void push_event(termole_event_type type, int32_t exit_code = 0, std::string text = {});

    mutable std::mutex mutex_;
    std::mutex terminate_mutex_;
    std::mutex event_mutex_;
    termole_vt* vt_ = nullptr;
    uint64_t revision_ = 1;

    std::unique_ptr<platform::WindowsPty> pty_;
    win::unique_handle data_event_;
    std::thread reader_;
    std::thread exit_thread_;

    std::atomic<bool> running_{ true };
    std::atomic<bool> stopping_{ false };
    std::atomic<bool> view_attached_{ false };
    std::atomic<int32_t> exit_code_{ 0 };
    std::deque<EventRecord> events_;
};

} // namespace termole
