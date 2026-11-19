#pragma once

#include "platform/pty.h"

#include <windows.h>

#include <string>

namespace termole::platform {

// Minimal Windows ConPTY backend for the platform::Pty contract. It supports
// argv/cwd/size and deliberately omits the file/handle redirection features of
// NativeSession until NativeSession is migrated onto this interface.
class WindowsPty final : public Pty {
public:
    static std::unique_ptr<Pty> spawn(const PtyOptions& options, std::string& error);
    ~WindowsPty() override;

    ssize_t read(void* buffer, size_t capacity) override;
    ssize_t write(const void* data, size_t size) override;
    bool resize(unsigned short columns, unsigned short rows, std::string& error) override;
    int wait() override;
    void close() override;

private:
    WindowsPty() = default;

    HANDLE input_write_ = nullptr;
    HANDLE output_read_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    HPCON console_ = nullptr;
    bool closed_ = false;
};

} // namespace termole::platform
