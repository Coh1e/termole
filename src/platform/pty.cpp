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
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

namespace termole::platform {

namespace {

class PosixPtyBackend final : public Pty {
public:
    PosixPtyBackend() = default;

    ~PosixPtyBackend() override
    {
        close();
        (void)wait();
        close_posix_pty(pty_);
        if (cancel_[0] >= 0) ::close(cancel_[0]);
        if (cancel_[1] >= 0) ::close(cancel_[1]);
    }

    bool start(const PtyOptions& options, std::string& error)
    {
        if (::pipe(cancel_) != 0) {
            error = std::string("pipe: ") + std::strerror(errno);
            return false;
        }
        for (const int fd : cancel_) {
            if (::fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
                error = std::string("fcntl: ") + std::strerror(errno);
                return false;
            }
        }
        return spawn_posix_pty(PosixPtyOptions{ options.arguments, options.working_directory,
                                                 options.columns, options.rows },
                               pty_, error);
    }

    ssize_t read(void* buffer, size_t capacity) override
    {
        while (!closed_.load()) {
            pollfd sources[]{ { cancel_[0], POLLIN, 0 }, { pty_.master, POLLIN, 0 } };
            const int ready = ::poll(sources, 2, -1);
            if (ready < 0 && errno == EINTR) continue;
            if (ready <= 0) return -1;
            if (sources[0].revents) return 0;
            const auto bytes = read_posix_pty(pty_, buffer, capacity);
            if (bytes < 0 && errno == EINTR) continue;
            return bytes < 0 && errno == EIO ? 0 : bytes;
        }
        return 0;
    }
    ssize_t write(const void* data, size_t size) override
    {
        if (closed_.load()) return -1;
        return write_posix_pty(pty_, data, size);
    }
    bool resize(unsigned short columns, unsigned short rows, std::string& error) override
    {
        if (closed_.load()) {
            error = "pty is closed";
            return false;
        }
        return resize_posix_pty(pty_, columns, rows, error);
    }
    int wait() override
    {
        for (;;) {
            {
                std::lock_guard lock(child_mutex_);
                if (pty_.child <= 0) return exit_code_;
                int status = 0;
                const pid_t waited = ::waitpid(pty_.child, &status, WNOHANG);
                if (waited == pty_.child || (waited == -1 && errno != EINTR)) {
                    pty_.child = -1;
                    if (waited > 0 && WIFEXITED(status)) exit_code_ = WEXITSTATUS(status);
                    if (waited > 0 && WIFSIGNALED(status)) exit_code_ = 128 + WTERMSIG(status);
                    return exit_code_;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    void close() override
    {
        if (closed_.exchange(true)) return;
        const char canceled = 1;
        if (cancel_[1] >= 0) (void)::write(cancel_[1], &canceled, sizeof(canceled));
        std::lock_guard lock(child_mutex_);
        if (pty_.child <= 0) return;
        // The PTY master stays valid until the reader observes cancellation.
        // Stop both the foreground job and the shell before waiting for them.
        const pid_t foreground = ::tcgetpgrp(pty_.master);
        if (foreground > 0 && foreground != pty_.child)
            (void)::kill(-foreground, SIGKILL);
        if (::kill(-pty_.child, SIGKILL) != 0)
            (void)::kill(pty_.child, SIGKILL);
    }

private:
    PosixPty pty_;
    int cancel_[2]{ -1, -1 };
    std::atomic<bool> closed_{ false };
    std::mutex child_mutex_;
    int exit_code_ = -1;
};

} // namespace

std::unique_ptr<Pty> Pty::spawn(const PtyOptions& options, std::string& error)
{
    auto pty = std::make_unique<PosixPtyBackend>();
    if (!pty->start(options, error)) return nullptr;
    return pty;
}

} // namespace termole::platform
#endif
