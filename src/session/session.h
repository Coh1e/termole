#pragma once

#include "common/error.h"
#include "common/limits.h"
#include "common/win.h"
#include "core/terminal.h"
#include "core/utf8.h"

#include <termole/termole.h>

#include <atomic>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace termole {

struct SessionOptions {
    std::wstring program;
    std::vector<std::wstring> arguments;
    std::wstring working_directory;
    std::vector<std::wstring> environment;
    std::wstring stdin_path;
    std::wstring stdout_path;
    uint16_t columns = 80;
    uint16_t rows = 24;
};

// Owns the ConPTY, the managed process tree and the terminal screen. It does
// not create a top-level window and can continue consuming output with no View
// attached.
class Session {
public:
    static Result<std::shared_ptr<Session>> create(const SessionOptions& options);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ~Session();

    Result<void> resize(uint16_t columns, uint16_t rows);
    Result<void> write(std::string_view bytes);
    Result<void> send_key(termole_key key);
    Result<void> terminate();

    [[nodiscard]] Snapshot snapshot(int scroll_offset = 0) const;
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

    // The Session owns at most one View, as required by the accepted design.
    [[nodiscard]] bool attach_view() noexcept;
    void detach_view() noexcept;

private:
    explicit Session(uint16_t columns, uint16_t rows);

    void read_loop();
    void exit_loop();
    Result<void> write_raw(std::string_view bytes);
    void push_event(termole_event_type type, int32_t exit_code = 0, std::string text = {});

    mutable std::mutex mutex_;
    std::mutex input_mutex_;
    std::mutex terminate_mutex_;
    std::mutex pseudo_mutex_;
    std::mutex event_mutex_;
    Terminal terminal_;
    Utf8Decoder decoder_;

    win::unique_handle input_write_;
    win::unique_handle output_read_;
    win::unique_handle process_;
    win::unique_handle job_;
    win::unique_handle data_event_;
    win::unique_pseudo_console pseudo_console_;
    std::thread reader_;
    std::thread exit_thread_;

    std::atomic<bool> running_{ true };
    std::atomic<bool> stopping_{ false };
    std::atomic<bool> view_attached_{ false };
    std::atomic<int32_t> exit_code_{ 0 };
    std::deque<EventRecord> events_;
};

} // namespace termole
