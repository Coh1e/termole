#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <cstdlib>
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
    if (argc < 2) return 2;
    const std::filesystem::path lazygit = argv[1];
    const auto repo = std::filesystem::temp_directory_path() /
        ("termole-lazygit-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(repo, error);
    {
        std::ofstream file(repo / "README.md", std::ios::binary);
        file << "initial\r\n";
    }
    const std::wstring quoted = L"\"" + repo.wstring() + L"\"";
    const std::wstring setup =
        L"git init -q " + quoted + L" && git -C " + quoted +
        L" config user.email termole@example.invalid && git -C " + quoted +
        L" config user.name Termole && git -C " + quoted + L" add . && git -C " + quoted +
        L" commit -qm initial";
    if (::_wsystem(setup.c_str()) != 0) {
        std::cerr << "could not prepare the test repository\n";
        return 1;
    }

    termole::Session::Options options;
    options.program = lazygit.wstring();
    options.working_directory = repo.wstring();
    options.columns = 120;
    options.rows = 35;
    options.environment = { L"TERM=xterm-256color" };
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "could not start lazygit: " << session.error().message() << '\n';
        return 1;
    }
    const bool drew = wait_for_text(*session, "Files", 1000) ||
        wait_for_text(*session, "Branches", 1000) || wait_for_text(*session, "Status", 1000) ||
        wait_for_text(*session, "\xE6\x96\x87\xE4\xBB\xB6", 1000) ||
        wait_for_text(*session, "\xE7\x8A\xB6\xE6\x80\x81", 1000) ||
        wait_for_text(*session, "\xE5\xB7\xAE\xE5\xBC\x82", 1000);
    if (!drew) {
        const auto screen = session->screen_text();
        std::cerr << "lazygit did not draw its panels. screen:\n"
                  << (screen ? *screen : std::string("<none>")) << '\n';
        (void)session->terminate();
        std::filesystem::remove_all(repo, error);
        return 1;
    }
    (void)session->send_key(termole::Key::Escape);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    (void)session->write("q");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session->running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const bool exited = !session->running();
    (void)session->terminate();
    std::filesystem::remove_all(repo, error);
    if (!exited) {
        std::cerr << "lazygit did not exit\n";
        return 1;
    }
    std::cout << "PASS: lazygit panels and exit\n";
    return 0;
}
