#pragma once

#include "platform/pty.h"

#include "common/win.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace termole::platform {

struct WindowsPtyOptions {
    std::wstring program;
    std::vector<std::wstring> arguments;
    std::wstring working_directory;
    std::vector<std::wstring> environment;
    std::wstring stdin_path;
    std::wstring stdout_path;
    // Direct child stdin/stdout handles (application data channel). Exclusive
    // with the file paths above; both are set together.
    HANDLE stdin_handle = nullptr;
    HANDLE stdout_handle = nullptr;
    uint16_t columns = 80;
    uint16_t rows = 24;
};

class WindowsPty final : public Pty {
public:
    static std::unique_ptr<Pty> spawn(const PtyOptions& options, std::string& error);
    static Result<std::unique_ptr<WindowsPty>> spawn_native(const WindowsPtyOptions& options);
    ~WindowsPty() override;

    Result<size_t> read_output(void* buffer, size_t capacity);
    Result<void> write_input(std::string_view bytes);
    Result<void> resize_console(uint16_t columns, uint16_t rows);
    int32_t wait_for_exit() const;
    Result<void> terminate_process(bool was_running);
    void close_input();
    // A session reader must keep draining output until the console closes.
    void close_console();

    ssize_t read(void* buffer, size_t capacity) override;
    ssize_t write(const void* data, size_t size) override;
    bool resize(unsigned short columns, unsigned short rows, std::string& error) override;
    int wait() override;
    void close() override;

private:
    WindowsPty() = default;

    std::mutex input_mutex_;
    std::mutex pseudo_mutex_;
    win::unique_handle input_write_;
    win::unique_handle output_read_;
    // Child stderr stays attached to the terminal in direct stdio mode.
    win::unique_handle console_output_write_;
    win::unique_handle process_;
    win::unique_handle job_;
    win::unique_pseudo_console pseudo_console_;
};

} // namespace termole::platform
