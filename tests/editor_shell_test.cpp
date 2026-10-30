#include <termole/termole.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return false;
}

bool wait_stopped(termole::Session& session, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (session.running()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return true;
}

std::string utf8(const std::wstring& text)
{
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string bytes(static_cast<size_t>(std::max(0, length)), '\0');
    if (length > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(),
                              length, nullptr, nullptr);
    }
    return bytes;
}

std::string contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    const std::wstring command = argv[1];
    const std::wstring mode = argv[2];
    const auto directory = std::filesystem::temp_directory_path() /
        ("termole-editor-shell-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto file = directory / "shell.txt";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "initial\r\n";
    }

    termole::Session::Options options;
    options.program = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = { L"/D", L"/Q" };
    options.working_directory = directory.wstring();
    options.columns = 100;
    options.rows = 30;
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "cmd failed: " << session.error().message() << '\n';
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    (void)session->write(utf8(command + L" " + file.wstring()));
    (void)session->send_key(termole::Key::Enter);
    std::this_thread::sleep_for(std::chrono::milliseconds(1400));
    (void)session->write("iHELLO-SHELL");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void)session->send_key(termole::Key::Escape);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void)session->write(":wq");
    (void)session->send_key(termole::Key::Enter);

    const bool saved = wait_for_text(*session, "HELLO-SHELL", 5000);
    const bool prompt = wait_for_text(*session, ">", 5000);
    const std::string text = contents(file);
    (void)session->write("exit");
    (void)session->send_key(termole::Key::Enter);
    const bool exited = wait_stopped(*session, 5000);
    const auto screen = session->screen_text();
    (void)session->terminate();
    std::filesystem::remove_all(directory, error);

    if (!saved || text.find("HELLO-SHELL") == std::string::npos) {
        std::cerr << "editor did not save inside cmd. screen:\n"
                  << (screen ? *screen : std::string("<none>")) << '\n';
        return 1;
    }
    if (!prompt || !exited) {
        std::cerr << "primary screen/prompt was not restored after " << utf8(mode)
                  << ". screen:\n" << (screen ? *screen : std::string("<none>")) << '\n';
        return 1;
    }
    std::cout << "PASS: editor inside cmd restores primary screen\n";
    return 0;
}
