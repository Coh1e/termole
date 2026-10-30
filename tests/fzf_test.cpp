#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {
bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto text = session.screen_text();
        if (text && text->find(marker) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const std::wstring fzf = argc > 1 ? argv[1] : L"fzf.exe";
    const auto root = std::filesystem::temp_directory_path() /
        ("termole-fzf-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto candidates = root / "candidates.txt";
    const auto script = root / "run.cmd";
    {
        std::ofstream file(candidates, std::ios::binary);
        file << "apple\r\nbanana\r\ncherry\r\n";
    }
    {
        std::ofstream file(script, std::ios::binary);
        file << "@echo off\r\n"
             << "type \"" << candidates.string() << "\" | \"" << std::filesystem::path(fzf).string()
             << "\"\r\n";
    }

    termole::Session::Options options;
    options.program = L"cmd.exe";
    options.arguments = { L"/D", L"/Q", L"/C", script.wstring() };
    options.columns = 80;
    options.rows = 20;
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "could not start fzf: " << session.error().message() << '\n';
        return 1;
    }

    if (!wait_for_text(*session, "banana", 10000)) {
        const auto text = session->screen_text();
        std::cerr << "fzf did not draw the candidate list. screen:\n"
                  << (text ? *text : std::string("<none>")) << '\n';
        (void)session->terminate();
        std::filesystem::remove_all(root, error);
        return 1;
    }

    (void)session->write("ban");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void)session->send_key(termole::Key::Enter);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session->running() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const bool exited = !session->running();
    (void)session->terminate();
    std::filesystem::remove_all(root, error);
    if (!exited) {
        std::cerr << "fzf did not exit after Enter\n";
        return 1;
    }
    std::cout << "PASS: real fzf draws, accepts typing and exits\n";
    return 0;
}
