#pragma once

#include "platform/pty.h"

#include <sys/types.h>

#include <string>
#include <vector>

namespace termole::platform {

struct PosixPtyOptions {
    std::vector<std::string> arguments;
    std::string working_directory;
    unsigned short columns = 80;
    unsigned short rows = 24;
};

struct PosixPty {
    int master = -1;
    pid_t child = -1;
};

// POSIX sessions can wait for output space without holding the VT lock,
// then write and account for accepted bytes in one critical section.
class PollablePty : public Pty {
public:
    static std::unique_ptr<PollablePty> spawn(const PtyOptions& options, std::string& error);
    virtual bool wait_writable() = 0;
    // A nonempty write returns 0 on transient backpressure, -1 on failure.
    virtual ssize_t try_write(const void* data, size_t size) = 0;
};

bool spawn_posix_pty(const PosixPtyOptions& options, PosixPty& pty, std::string& error);
bool resize_posix_pty(PosixPty& pty, unsigned short columns, unsigned short rows,
                      std::string& error);
ssize_t read_posix_pty(PosixPty& pty, void* buffer, size_t capacity);
ssize_t write_posix_pty(PosixPty& pty, const void* data, size_t size);
int wait_posix_pty(PosixPty& pty);
void close_posix_pty(PosixPty& pty);

} // namespace termole::platform
