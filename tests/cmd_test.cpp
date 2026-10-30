#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {
bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto text = session.screen_text();
        if (text && text->find(marker) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}
} // namespace

int wmain()
{
    termole::Session::Options options;
    options.program = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = { L"/D", L"/Q" };
    options.columns = 80;
    options.rows = 24;
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "create failed: " << session.error().message() << '\n';
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    (void)session->write("echo CMD-OK");
    (void)session->send_key(termole::Key::Enter);
    const bool echoed = wait_for_text(*session, "CMD-OK", 5000);
    (void)session->write("exit");
    (void)session->send_key(termole::Key::Enter);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session->running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    const bool exited = !session->running();
    const auto code = session->exit_code();
    const auto screen = session->screen_text();
    (void)session->terminate();
    std::cout << "echoed=" << echoed << " exited=" << exited
              << " code=" << (code ? std::to_string(*code) : "?") << '\n';
    if (!echoed || !exited || (code && *code != 0)) {
        std::cerr << "screen:\n" << (screen ? *screen : std::string("<none>")) << '\n';
        return 1;
    }
    std::cout << "PASS: interactive cmd echo/exit\n";
    return 0;
}
