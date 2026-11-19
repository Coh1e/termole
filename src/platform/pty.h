#pragma once

#include <sys/types.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace termole::platform {

struct PtyOptions {
    std::vector<std::string> arguments;
    std::string working_directory;
    unsigned short columns = 80;
    unsigned short rows = 24;
};

// Platform-neutral PTY contract. POSIX is implemented; Windows ConPTY will
// implement the same interface before NativeSession is refactored.
class Pty {
public:
    static std::unique_ptr<Pty> spawn(const PtyOptions& options, std::string& error);

    virtual ~Pty() = default;
    Pty(const Pty&) = delete;
    Pty& operator=(const Pty&) = delete;

    virtual ssize_t read(void* buffer, size_t capacity) = 0;
    virtual ssize_t write(const void* data, size_t size) = 0;
    virtual bool resize(unsigned short columns, unsigned short rows, std::string& error) = 0;
    virtual int wait() = 0;
    virtual void close() = 0;

protected:
    Pty() = default;
};

} // namespace termole::platform
