#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {
bool wait_stopped(termole::Session& session, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (session.running()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return true;
}

std::string contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

bool edit(const std::filesystem::path& editor, const std::filesystem::path& directory,
          const std::filesystem::path& file, const std::vector<std::wstring>& arguments)
{
    termole::Session::Options options;
    options.program = editor.wstring();
    options.arguments = arguments;
    options.arguments.push_back(file.wstring());
    options.working_directory = directory.wstring();
    options.columns = 100;
    options.rows = 30;
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "could not start editor: " << session.error().message() << '\n';
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    (void)session->write("iHELLO-T18");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void)session->send_key(termole::Key::Escape);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    (void)session->write(":wq");
    (void)session->send_key(termole::Key::Enter);
    const bool exited = wait_stopped(*session, 15000);
    const std::string text = contents(file);
    const auto screen = session->screen_text();
    (void)session->terminate();
    if (!exited) {
        std::cerr << "editor did not exit. screen:\n"
                  << (screen ? *screen : std::string("<none>")) << '\n';
        return false;
    }
    return text.find("HELLO-T18") != std::string::npos;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    const std::filesystem::path editor = argv[1];
    const std::wstring mode = argv[2];
    const auto directory = std::filesystem::temp_directory_path() /
        ("termole-editor-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto file = directory / "edit.txt";
    {
        std::ofstream stream(file, std::ios::binary);
        stream << "initial\r\n";
    }

    bool ok = false;
    if (mode == L"nvim") {
        ok = edit(editor, directory, file, { L"-u", L"NONE", L"-n", L"-i", L"NONE" });
    } else if (mode == L"helix") {
        ok = edit(editor, directory, file, {});
    }
    std::filesystem::remove_all(directory, error);
    if (!ok) {
        std::cerr << "editor flow failed\n";
        return 1;
    }
    std::cout << "PASS: editor insert/save/quit\n";
    return 0;
}
