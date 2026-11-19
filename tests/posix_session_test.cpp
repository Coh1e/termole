#include "platform/posix_pty.h"

#include <termole/core.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
    options.arguments = { "/bin/sh", "-c", "printf POSIX-SESSION; exit 5" };
    options.columns = 80;
    options.rows = 24;
    PosixPty pty;
    std::string error;
    check(spawn_posix_pty(options, pty, error), "spawn posix session pty");
    if (pty.master < 0) return 1;

    termole_vt* vt = nullptr;
    check(termole_vt_create(80, 24, &vt) == TERMOLE_CORE_OK, "create Linux C core");
    if (!vt) return 1;

    char buffer[512];
    for (;;) {
        const ssize_t read = read_posix_pty(pty, buffer, sizeof(buffer));
        if (read <= 0) break;
        check(termole_vt_feed(vt, buffer, static_cast<size_t>(read)) == TERMOLE_CORE_OK,
              "feed pty bytes into Linux core");
        if (read > 0) break; /* one chunk is enough for this probe; loop below drains */
    }
    /* Drain until EOF without repeating the check. */
    for (;;) {
        const ssize_t read = read_posix_pty(pty, buffer, sizeof(buffer));
        if (read <= 0) break;
        (void)termole_vt_feed(vt, buffer, static_cast<size_t>(read));
    }

    const int exit_code = wait_posix_pty(pty);
    check(exit_code == 5, "posix session exit code");

    char text[512];
    size_t length = 0;
    check(termole_vt_text(vt, text, sizeof(text), &length) == TERMOLE_CORE_OK, "render text");
    check(std::string(text, length).find("POSIX-SESSION") != std::string::npos,
          "screen text contains child output");

    close_posix_pty(pty);
    termole_vt_free(vt);

    if (failures != 0) {
        std::fprintf(stderr, "%d posix session check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: posix session\n");
    return 0;
}
