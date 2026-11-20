#include "platform/posix_pty.h"

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool condition, const char* what)
{
    if (condition) std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main()
{
    using namespace termole::platform;
    PosixPtyOptions options;
    options.arguments = { "/bin/sh", "-c", "printf POSIX-PTY-OK; exit 7" };
    options.columns = 100;
    options.rows = 30;
    PosixPty pty;
    std::string error;
    check(spawn_posix_pty(options, pty, error), "spawn posix pty");
    if (pty.master < 0) return 1;

    std::string output;
    char buffer[256];
    for (;;) {
        const ssize_t read = read_posix_pty(pty, buffer, sizeof(buffer));
        if (read <= 0) break;
        output.append(buffer, static_cast<size_t>(read));
    }
    check(output.find("POSIX-PTY-OK") != std::string::npos, "child output arrives");
    check(wait_posix_pty(pty) == 7, "child exit code preserved");
    close_posix_pty(pty);

    if (failures != 0) {
        std::fprintf(stderr, "%d posix pty check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: posix pty\n");
    return 0;
}
