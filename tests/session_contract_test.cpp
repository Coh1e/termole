#ifdef _WIN32
#include "session/session.h"
#else
#include "platform/posix_session.h"
#endif

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main()
{
    using namespace std::chrono_literals;
#ifdef _WIN32
    termole::SessionOptions options;
    options.program = L"cmd.exe";
    options.arguments = { L"/d", L"/q" };
    auto result = termole::NativeSession::create(options);
    if (!result) {
        std::fprintf(stderr, "%s\n", result.error().message.c_str());
        return 1;
    }
    auto& session = *result;
    const auto resize = [&] { return session->resize(100, 30).has_value(); };
    const auto write = [&](std::string_view bytes) { return session->write(bytes).has_value(); };
    const auto text = [&] { return session->screen_text(); };
    const auto ended = [&] { return !session->running(); };
    const auto exit_code = [&] { return session->exit_code().value_or(-1); };
    const std::string_view print = "set VAR=READY\recho SESSION-%VAR%\r";
    const std::string_view quit = "exit /b 7\r";
#else
    std::string error;
    termole::platform::PosixSessionOptions options;
    options.arguments = { "/bin/sh" };
    auto session = termole::platform::PosixSession::create(options, error);
    if (!session) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const auto resize = [&] { return session->resize(100, 30, error); };
    const auto write = [&](std::string_view bytes) { return session->write(bytes, error); };
    const auto text = [&] { return session->text(); };
    const auto ended = [] { return true; };
    const auto exit_code = [&] { return session->wait(); };
    const std::string_view print = "printf 'SESSION-%s\\n' READY\r";
    const std::string_view quit = "exit 7\r";
#endif

    const auto wait_until = [](const auto& predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(20ms);
        return predicate();
    };
    if (!resize() || !write(print)) {
        std::fputs("Could not configure or write to the session\n", stderr);
        return 1;
    }
    if (!wait_until([&] { return text().find("SESSION-READY") != std::string::npos; })) {
        std::fputs("The session did not display the shell response\n", stderr);
        return 1;
    }
    if (!write(quit) || !wait_until(ended) || exit_code() != 7) {
        std::fputs("The session did not report exit code 7\n", stderr);
        return 1;
    }
    return 0;
}
