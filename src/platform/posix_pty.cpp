#include "platform/posix_pty.h"

#include <fcntl.h>
#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace termole::platform {

namespace {

void set_size(int fd, unsigned short columns, unsigned short rows)
{
    winsize size{};
    size.ws_col = columns;
    size.ws_row = rows;
    (void)ioctl(fd, TIOCSWINSZ, &size);
}

} // namespace

bool spawn_posix_pty(const PosixPtyOptions& options, PosixPty& pty, std::string& error)
{
    if (options.arguments.empty() || options.arguments.front().empty()) {
        error = "posix pty requires a program";
        return false;
    }

    std::vector<char*> argv;
    argv.reserve(options.arguments.size() + 1);
    for (const std::string& argument : options.arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    int master = -1;
    int slave = -1;
    if (openpty(&master, &slave, nullptr, nullptr, nullptr) != 0) {
        error = std::string("openpty: ") + std::strerror(errno);
        return false;
    }
    set_size(master, options.columns, options.rows);
    set_size(slave, options.columns, options.rows);

    const pid_t child = fork();
    if (child < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        close(master);
        close(slave);
        return false;
    }
    if (child == 0) {
        setsid();
        (void)ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO) close(slave);
        close(master);
        if (!options.working_directory.empty() &&
            chdir(options.working_directory.c_str()) != 0) {
            _exit(126);
        }
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(slave);
    pty.master = master;
    pty.child = child;
    return true;
}

bool resize_posix_pty(PosixPty& pty, unsigned short columns, unsigned short rows,
                      std::string& error)
{
    if (pty.master < 0) {
        error = "pty is not open";
        return false;
    }
    winsize size{};
    size.ws_col = columns;
    size.ws_row = rows;
    if (ioctl(pty.master, TIOCSWINSZ, &size) != 0) {
        error = std::string("TIOCSWINSZ: ") + std::strerror(errno);
        return false;
    }
    return true;
}

ssize_t read_posix_pty(PosixPty& pty, void* buffer, size_t capacity)
{
    if (pty.master < 0) return -1;
    return read(pty.master, buffer, capacity);
}

ssize_t write_posix_pty(PosixPty& pty, const void* data, size_t size)
{
    if (pty.master < 0) return -1;
    return write(pty.master, data, size);
}

int wait_posix_pty(PosixPty& pty)
{
    if (pty.child <= 0) return -1;
    int status = 0;
    if (waitpid(pty.child, &status, 0) != pty.child) return -1;
    pty.child = -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

void close_posix_pty(PosixPty& pty)
{
    if (pty.master >= 0) close(pty.master);
    pty.master = -1;
}

} // namespace termole::platform
