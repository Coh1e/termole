#pragma once

#include "common/error.h"
#include "common/limits.h"
#include "common/win.h"
#include "core/terminal.h"
#include "core/utf8.h"

#include <termole/termole.h>

#include <atomic>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace termole {

struct SessionOptions {
    std::wstring program;
    std::wstring arguments;
    std::wstring working_directory;
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

    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] std::pair<uint16_t, uint16_t> size() const;
    [[nodiscard]] std::string screen_text() const;
    [[nodiscard]] std::wstring title() const;
    [[nodiscard]] bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    [[nodiscard]] HANDLE data_event() const noexcept { return data_event_.get(); }

    // The Session owns at most one View, as required by the accepted design.
    [[nodiscard]] bool attach_view() noexcept;
    void detach_view() noexcept;

private:
    explicit Session(uint16_t columns, uint16_t rows);

    void read_loop();
    Result<void> write_raw(std::string_view bytes);

    mutable std::mutex mutex_;
    std::mutex input_mutex_;
    std::mutex terminate_mutex_;
    Terminal terminal_;
    Utf8Decoder decoder_;

    win::unique_handle input_write_;
    win::unique_handle output_read_;
    win::unique_handle process_;
    win::unique_handle job_;
    win::unique_handle data_event_;
    win::unique_pseudo_console pseudo_console_;
    std::thread reader_;

    std::atomic<bool> running_{ true };
    std::atomic<bool> stopping_{ false };
    std::atomic<bool> view_attached_{ false };
};

} // namespace termole
