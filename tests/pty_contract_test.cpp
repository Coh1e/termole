#include "platform/pty.h"

#include <cstdio>
#include <string>

int main()
{
    using namespace termole::platform;
    std::string error;
    if (Pty::spawn({}, error) || error.empty()) {
        std::fputs("Empty PTY command should fail with a diagnostic\n", stderr);
        return 1;
    }

    PtyOptions options;
#ifdef _WIN32
    options.arguments = { "cmd.exe", "/d", "/c", "echo PTY-CONTRACT && exit /b 7" };
#else
    options.arguments = { "/bin/sh", "-c", "printf 'PTY-CONTRACT\\n'; exit 7" };
#endif
    auto pty = Pty::spawn(options, error);
    if (!pty) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    std::string output;
    char buffer[512];
    while (output.size() < 4096 && output.find("PTY-CONTRACT") == std::string::npos) {
        const auto count = pty->read(buffer, sizeof(buffer));
        if (count <= 0) break;
        output.append(buffer, static_cast<size_t>(count));
    }
    const bool received_output = output.find("PTY-CONTRACT") != std::string::npos;
    const int code = pty->wait();
    pty->close();
    if (!received_output || code != 7) {
        std::fprintf(stderr, "Expected PTY output and exit code 7, got exit code %d\n", code);
        return 1;
    }
    return 0;
}
