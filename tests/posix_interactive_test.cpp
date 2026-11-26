#include "platform/posix_session.h"

#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

static int failures = 0;

static void check(bool condition, const char* what)
{
    if (condition) std::printf("PASS: %s\n", what);
    else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static constexpr size_t paste_size = 128 * 1024;

static int duplex_child()
{
    // Bound both directions even when the session stops draining the PTY.
    ::alarm(5);
    termios mode{};
    if (::tcgetattr(STDIN_FILENO, &mode) < 0) return 1;
    ::cfmakeraw(&mode);
    if (::tcsetattr(STDIN_FILENO, TCSANOW, &mode) < 0) return 1;
    if (::write(STDOUT_FILENO, "DUPLEX-READY", 12) != 12) return 1;
    char first;
    if (::read(STDIN_FILENO, &first, 1) != 1 || first != 'p') return 2;

    // The paste is in progress. Produce more output than the PTY can buffer
    // before consuming the rest of the paste and both cursor reports.
    constexpr char queries[] = "\x1b[7;12H\x1b[6n\x1b[2;3H\x1b[6n";
    if (::write(STDOUT_FILENO, queries, sizeof(queries) - 1) != sizeof(queries) - 1) return 3;
    std::array<char, 4096> buffer{};
    for (size_t remaining = paste_size; remaining;) {
        const auto n = ::write(STDOUT_FILENO, buffer.data(), std::min(remaining, buffer.size()));
        if (n <= 0) return 4;
        remaining -= static_cast<size_t>(n);
    }
    for (size_t remaining = paste_size - 1; remaining;) {
        const auto n = ::read(STDIN_FILENO, buffer.data(), std::min(remaining, buffer.size()));
        if (n <= 0 || !std::all_of(buffer.begin(), buffer.begin() + n, [](char c) { return c == 'p'; }))
            return 5;
        remaining -= static_cast<size_t>(n);
    }
    for (char expected : std::string_view("\x1b[7;12R\x1b[2;3R")) {
        char received;
        if (::read(STDIN_FILENO, &received, 1) != 1 || received != expected) return 6;
    }
    return 0;
}

static bool duplex_session(const char* executable)
{
    using namespace std::chrono_literals;
    std::string error;
    termole::platform::PosixSessionOptions options;
    options.arguments = { executable, "--duplex-child" };
    auto session = termole::platform::PosixSession::create(options, error);
    if (!session) return false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (session->text().find("DUPLEX-READY") == std::string::npos) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(5ms);
    }
    const bool written = session->write(std::string(paste_size, 'p'), error);
    const int status = session->wait();
    if (!written || status != 0) {
        std::fprintf(stderr, "duplex child exit=%d, paste written=%d %s\n", status, written, error.c_str());
        return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--duplex-child") return duplex_child();
    using namespace termole::platform;
    PosixSessionOptions options;
    options.arguments = { "/bin/sh" };
    options.columns = 80;
    options.rows = 24;
    std::string error;
    auto session = PosixSession::create(options, error);
    check(session != nullptr, "create posix session");
    if (!session) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    check(session->resize(100, 30, error), "resize posix session");
    check(session->write("echo POSIX-READY\r\n", error), "write command to posix session");
    check(session->write("stty size\r\n", error), "request pty size");

    bool ready = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (session->text().find("POSIX-READY") != std::string::npos) {
            ready = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(ready, "interactive shell output reaches the core");

    bool sized = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (session->text().find("30 100") != std::string::npos) {
            sized = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(sized, "pty resize reaches the child (stty size)");

    check(session->write("printf '\344\270\255\346\226\207\n'\r\n", error),
          "write utf-8 text to posix session");
    bool utf8 = false;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (session->text().find("\xe4\xb8\xad\xe6\x96\x87") != std::string::npos) {
            utf8 = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(utf8, "utf-8 text reaches the core");

    check(session->write("exit 7\r\n", error), "write exit to posix session");
    check(session->wait() == 7, "interactive shell exit code");

    PosixSessionOptions reply_options;
    check(reply_options.max_queued_reply_bytes == 1024 * 1024, "default reply queue budget is one MiB");
    reply_options.arguments = { "/bin/sh", "-c", R"sh(
        stty -echo -icanon min 0 time 10 || exit 1
        printf '\033[7;12H\033[6n'
        reply=$(dd bs=1 count=7 2>/dev/null)
        [ "$reply" = "$(printf '\033[7;12R')" ] && exit 0
        printf 'Unexpected cursor report (hex): '
        printf '%s' "$reply" | od -An -tx1
        exit 1
    )sh" };
    // ESC [ 7 ; 1 2 R is exactly seven bytes. One callback delivers the
    // whole report, so the writer's scheduling cannot change either outcome.
    reply_options.max_queued_reply_bytes = 7;
    auto reporter = PosixSession::create(reply_options, error);
    check(reporter != nullptr, "start cursor report child");
    if (reporter) {
        const int status = reporter->wait();
        if (status != 0) std::fprintf(stderr, "%s\n", reporter->text().c_str());
        check(status == 0, "child receives the cursor position at row 7, column 12 through stdin");
    }

    reply_options.max_queued_reply_bytes = 6;
    auto overflow = PosixSession::create(reply_options, error);
    check(overflow != nullptr, "start child with a reply budget smaller than the cursor report");
    if (overflow) {
        check(overflow->wait() == 128 + SIGKILL, "reply overflow terminates and reaps the child");
        check(!overflow->running() && !overflow->write("x", error), "closed session rejects input");
        overflow.reset();
    }

    check(duplex_session(argv[0]), "child can finish output before reading a paste and ordered cursor reports");

    options.arguments = { "/bin/sh", "-c", "trap '' HUP; printf 'SESSION-ACTIVE'; sleep 60" };
    auto active = PosixSession::create(options, error);
    check(active != nullptr, "create a session while testing shutdown");
    if (active) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (active->text().find("SESSION-ACTIVE") == std::string::npos &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        check(active->text().find("SESSION-ACTIVE") != std::string::npos,
              "the child is running before the session is closed");
        const auto started = std::chrono::steady_clock::now();
        active.reset();
        check(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
              "destroy cancels the reader and reaps a running shell promptly");
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d posix interactive check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: posix interactive session\n");
    return 0;
}
