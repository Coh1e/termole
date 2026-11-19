#include "platform/pty.h"

#include "platform/posix_pty.h"

#if defined(_WIN32)
#include "platform/windows_pty.h"

namespace termole::platform {
std::unique_ptr<Pty> Pty::spawn(const PtyOptions& options, std::string& error)
{
    return WindowsPty::spawn(options, error);
}
} // namespace termole::platform
#else
namespace termole::platform {

namespace {

class PosixPtyBackend final : public Pty {
public:
    explicit PosixPtyBackend(PosixPty pty) : pty_(pty) {}

    ssize_t read(void* buffer, size_t capacity) override
    {
        return read_posix_pty(pty_, buffer, capacity);
    }
    ssize_t write(const void* data, size_t size) override
    {
        return write_posix_pty(pty_, data, size);
    }
    bool resize(unsigned short columns, unsigned short rows, std::string& error) override
    {
        return resize_posix_pty(pty_, columns, rows, error);
    }
    int wait() override { return wait_posix_pty(pty_); }
    void close() override { close_posix_pty(pty_); }

private:
    PosixPty pty_;
};

} // namespace

std::unique_ptr<Pty> Pty::spawn(const PtyOptions& options, std::string& error)
{
    PosixPty pty;
    if (!spawn_posix_pty(PosixPtyOptions{ options.arguments, options.working_directory,
                                          options.columns, options.rows },
                         pty, error)) {
        return nullptr;
    }
    return std::make_unique<PosixPtyBackend>(pty);
}

} // namespace termole::platform
#endif
