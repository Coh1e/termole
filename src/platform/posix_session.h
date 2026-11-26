#pragma once

#include <termole/core.h>

#include "core/frame.h"
#include "platform/pty.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
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

using PosixFrame = SessionFrame;

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
    std::optional<PosixFrame> frame_after(uint64_t revision) const;
    termole_modes modes() const;
    bool running() const;
    int wait();

private:
    PosixSession() = default;
    bool write_all(std::string_view bytes);
    void write_replies();
    void read_loop();
    void stop();

    std::unique_ptr<Pty> pty_;
    termole_vt* vt_ = nullptr;
    mutable std::mutex mutex_;
    std::string replies_;
    std::condition_variable replies_ready_;
    std::mutex input_mutex_;
    std::thread reader_;
    std::thread writer_;
    bool stopped_ = false;
    int exit_code_ = -1;
    uint64_t revision_ = 1;
};

} // namespace termole::platform
