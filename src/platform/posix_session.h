#pragma once

#include <termole/core.h>

#include "platform/pty.h"

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace termole::platform {

struct PosixSessionOptions {
    std::vector<std::string> arguments;
    std::string working_directory;
    unsigned short columns = 80;
    unsigned short rows = 24;
};

// Minimal interactive POSIX session: PTY reader thread feeds the C core.
// This is the Linux counterpart of NativeSession; it deliberately exposes
// only a small surface until the platform/pty interface is shared.
class PosixSession {
public:
    static std::unique_ptr<PosixSession> create(const PosixSessionOptions& options,
                                                std::string& error);
    ~PosixSession();

    PosixSession(const PosixSession&) = delete;
    PosixSession& operator=(const PosixSession&) = delete;

    bool write(std::string_view bytes, std::string& error);
    bool resize(unsigned short columns, unsigned short rows, std::string& error);
    std::string text() const;
    int wait();

private:
    PosixSession() = default;
    void read_loop();
    void stop();

    std::unique_ptr<Pty> pty_;
    termole_vt* vt_ = nullptr;
    mutable std::mutex mutex_;
    std::thread reader_;
    bool stopped_ = false;
    int exit_code_ = -1;
};

} // namespace termole::platform
