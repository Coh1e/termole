#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, std::string_view what)
{
    if (condition) {
        std::cout << "PASS: " << what << '\n';
    } else {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::string utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size,
                          nullptr, nullptr);
    return result;
}

std::wstring quote(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
        backslashes = 0;
        if (character == L'"') result.push_back(L'\\');
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

std::string read_file(const std::wstring& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool report_ready(const std::string& report)
{
    return report.find("stdout.console=") != std::string::npos;
}

std::string line_value(const std::string& report, std::string_view key)
{
    const std::string prefix = std::string(key) + "=";
    size_t position = 0;
    while (position < report.size()) {
        const size_t end = report.find('\n', position);
        const std::string_view line(report.data() + position,
                                    (end == std::string::npos ? report.size() : end) - position);
        if (line.substr(0, prefix.size()) == prefix) return std::string(line.substr(prefix.size()));
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return {};
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 5) {
        std::cerr << "usage: cli_no_console_test <termole.exe> <helper> <cwd> <report>\n";
        return 2;
    }

    const std::wstring app = argv[1];
    const std::wstring helper = argv[2];
    const std::wstring cwd = argv[3];
    const std::wstring report = argv[4];

    std::error_code error;
    std::filesystem::create_directories(cwd, error);
    std::filesystem::remove(report, error);

    std::wstring command = quote(app) + L" --cwd " + quote(cwd) + L" --env " +
        quote(L"TERMOLE_PROBE_REPORT=" + report) + L" --env " +
        quote(L"TERMOLE_PROBE_VALUE=no console") + L" -- " + quote(helper) + L" " +
        quote(L"alpha") + L" " + quote(L"a b");

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                          nullptr, nullptr, &startup, &process)) {
        std::cerr << "CreateProcessW failed: " << ::GetLastError() << '\n';
        return 1;
    }
    ::CloseHandle(process.hThread);
    const DWORD wait = ::WaitForSingleObject(process.hProcess, 30000);
    DWORD exit_code = 1;
    if (wait == WAIT_OBJECT_0) ::GetExitCodeProcess(process.hProcess, &exit_code);
    ::CloseHandle(process.hProcess);
    check(wait == WAIT_OBJECT_0, "no-console launch exits before the timeout");
    check(exit_code == 0, "no-console launch returns success");

    std::string content;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!report_ready(content) && std::chrono::steady_clock::now() < deadline) {
        content = read_file(report);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(report_ready(content), "no-console launch produced a child report");
    if (!report_ready(content)) return 1;

    check(line_value(content, "argc") == "3", "no-console launch preserved the argument count");
    check(line_value(content, "arg1") == "alpha", "no-console launch preserved the first argument");
    check(line_value(content, "arg2") == "a b", "no-console launch preserved a spaced argument");
    check(line_value(content, "cwd") == utf8(std::filesystem::path(cwd).make_preferred().wstring()),
          "no-console launch preserved the working directory");
    check(line_value(content, "env.TERMOLE_PROBE_VALUE") == "no console",
          "no-console launch preserved the environment override");
    check(line_value(content, "stdout.console") == "1",
          "no-console launch still gives the target a real ConPTY console");
    check(line_value(content, "stdin.console") == "1",
          "no-console launch gives the target a ConPTY stdin");
    check(line_value(content, "stderr.console") == "1",
          "no-console launch gives the target a ConPTY stderr");

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: no-console CLI launch\n";
    return 0;
}
