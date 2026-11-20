#include "platform/posix_session.h"

#include <chrono>
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

int main()
{
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
