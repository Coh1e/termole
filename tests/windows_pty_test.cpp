#include "platform/pty.h"
#include "platform/windows_pty.h"

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
    WindowsPtyOptions invalid;
    invalid.program = L"cmd.exe";
    invalid.stdout_path = L"unused-output.txt";
    auto missing_path = WindowsPty::spawn_native(invalid);
    check(!missing_path && missing_path.error().status == TERMOLE_STATUS_INVALID_ARGUMENT,
          "reject incomplete file redirection before opening paths");

    invalid.stdout_path.clear();
    invalid.stdin_handle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (!invalid.stdin_handle) invalid.stdin_handle = INVALID_HANDLE_VALUE;
    auto missing_handle = WindowsPty::spawn_native(invalid);
    check(!missing_handle && missing_handle.error().status == TERMOLE_STATUS_INVALID_ARGUMENT,
          "reject incomplete direct handle redirection before spawning");

    PtyOptions options;
    options.arguments = { "cmd.exe", "/c", "echo", "WINDOWS-PTY" };
    options.columns = 80;
    options.rows = 24;
    std::string error;
    auto pty = Pty::spawn(options, error);
    check(pty != nullptr, "spawn windows ConPTY");
    if (!pty) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    check(pty->resize(100, 30, error), "resize windows ConPTY");

    std::string output;
    char buffer[1024];
    bool found = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const ssize_t read = pty->read(buffer, sizeof(buffer));
        if (read > 0) {
            output.append(buffer, static_cast<size_t>(read));
            if (output.find("WINDOWS-PTY") != std::string::npos) {
                found = true;
                break;
            }
        }
    }
    check(found, "ConPTY child output arrives");
    check(pty->wait() == 0, "ConPTY child exit code");
    pty->close();

    if (failures != 0) {
        std::fprintf(stderr, "%d windows pty check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: windows ConPTY pty\n");
    return 0;
}
