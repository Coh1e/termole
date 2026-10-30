#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int wmain(int argc, wchar_t** argv)
{
    const std::wstring wsl = argc > 1 ? argv[1] : L"wsl.exe";
    termole::Session::Options options;
    options.program = wsl;
    options.arguments = { L"sh", L"-c", L"printf 'WSL-OK\\n'" };
    options.columns = 80;
    options.rows = 24;
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "could not start WSL: " << session.error().message() << '\n';
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (session->running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const auto screen = session->screen_text();
    (void)session->terminate();
    if (!screen || screen->find("WSL-OK") == std::string::npos) {
        std::cerr << "WSL did not produce expected output. screen:\n"
                  << (screen ? *screen : std::string("<none>")) << '\n';
        return 1;
    }
    std::cout << "PASS: WSL shell output through the public Session\n";
    return 0;
}
