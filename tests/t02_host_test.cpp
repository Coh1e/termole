#include <termole/termole.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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

std::filesystem::path module_path()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

std::filesystem::path helper_path()
{
    auto path = module_path();
    path.replace_filename(L"cli-report-helper.exe");
    return path;
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

std::optional<std::string> read_if_ready(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string text{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    if (text.find("stdout.console=") == std::string::npos) return std::nullopt;
    return text;
}

std::optional<std::string> wait_for_report(const std::filesystem::path& path, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        if (auto text = read_if_ready(path)) return text;
        if (std::chrono::steady_clock::now() >= deadline) return std::nullopt;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool wait_for_stopped(termole::Session& session, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (session.running()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
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

struct Probe {
    std::filesystem::path directory;
    std::filesystem::path helper;
    std::filesystem::path report;
};

Probe make_probe(std::wstring_view suffix)
{
    Probe probe;
    probe.directory = std::filesystem::temp_directory_path() /
        (L"termole t02 " + std::to_wstring(::GetCurrentProcessId()) + L" " +
         std::wstring(suffix));
    std::error_code error;
    std::filesystem::remove_all(probe.directory, error);
    std::filesystem::create_directories(probe.directory, error);
    probe.helper = probe.directory / L"report helper.exe";
    probe.report = probe.directory / L"report.txt";
    std::filesystem::copy_file(helper_path(), probe.helper,
                               std::filesystem::copy_options::overwrite_existing, error);
    return probe;
}

void remove_probe(const Probe& probe)
{
    std::error_code error;
    std::filesystem::remove_all(probe.directory, error);
}

} // namespace

int wmain()
{
    const std::wstring caller_sentinel = L"caller-sentinel";
    const std::wstring caller_value = L"caller-value";
    check(::SetEnvironmentVariableW(L"TERMOLE_CALLER_SENTINEL", caller_sentinel.c_str()) != FALSE,
          "caller sentinel environment set for the inheritance probe");
    check(::SetEnvironmentVariableW(L"TERMOLE_PROBE_VALUE", caller_value.c_str()) != FALSE,
          "caller override value set for the no-mutation probe");

    Probe first = make_probe(L"first");
    Probe second = make_probe(L"second");
    check(std::filesystem::exists(first.helper), "helper copied to a path containing spaces");
    check(std::filesystem::exists(second.helper), "second helper copied");

    termole::Session::Options first_options;
    first_options.program = first.helper.wstring();
    first_options.arguments = { L"alpha", L"a b", L"\"quoted\"", L"", L"中文-🐭",
                                L"back\\slash\"quote", L"-dash" };
    first_options.working_directory = first.directory.wstring();
    first_options.environment = {
        L"TERMOLE_PROBE_REPORT=" + first.report.wstring(),
        L"TERMOLE_PROBE_VALUE=hello world",
        L"TERMOLE_PROBE_EXTRA=中文 value",
    };

    termole::Session::Options second_options;
    second_options.program = second.helper.wstring();
    second_options.arguments = { L"second", L"two words" };
    second_options.working_directory = second.directory.wstring();
    second_options.environment = {
        L"TERMOLE_PROBE_REPORT=" + second.report.wstring(),
        L"TERMOLE_PROBE_VALUE=second value",
    };

    auto first_result = termole::Session::create(first_options);
    auto second_result = termole::Session::create(second_options);
    check(first_result.has_value(), "first host session created");
    check(second_result.has_value(), "second host session created concurrently");

    if (first_result) {
        auto first_session = std::move(*first_result);
        const auto report = wait_for_report(first.report, 10000);
        check(report.has_value(), "first report written by the real console child");
        if (report) {
            check(line_value(*report, "argc") == "8", "first argc includes the program and exact arguments");
            check(line_value(*report, "arg1") == "alpha", "first argument preserved");
            check(line_value(*report, "arg2") == "a b", "argument with spaces preserved");
            check(line_value(*report, "arg3") == "\"quoted\"", "argument with quotes preserved");
            check(line_value(*report, "arg4len") == "0" && line_value(*report, "arg4").empty(),
                  "empty argument preserved as an empty argv entry");
            check(line_value(*report, "arg5") == utf8(L"中文-🐭"),
                  "Unicode argument preserved byte-exactly");
            check(line_value(*report, "arg6") == "back\\slash\"quote",
                  "backslashes and embedded quotes preserved");
            check(line_value(*report, "arg7") == "-dash", "dash argument is not parsed as a Termole option");
            check(line_value(*report, "arg0").ends_with("report helper.exe"),
                  "program path with spaces reaches the child as argv[0]");
            check(line_value(*report, "cwd") == utf8(first.directory.wstring()),
                  "working directory is the requested directory");
            check(line_value(*report, "env.TERMOLE_PROBE_VALUE") == "hello world",
                  "environment override reaches the child");
            check(line_value(*report, "env.TERMOLE_PROBE_EXTRA") == utf8(L"中文 value"),
                  "Unicode environment value reaches the child");
            check(line_value(*report, "env.TERMOLE_CALLER_SENTINEL") == utf8(caller_sentinel),
                  "caller environment is inherited and not mutated");
            check(!line_value(*report, "env.SystemRoot").empty(),
                  "inherited environment entries remain available");
            check(line_value(*report, "stdout.console") == "1",
                  "child standard output is a real ConPTY console");
            check(line_value(*report, "stdin.console") == "1",
                  "child standard input is a real ConPTY console");
            check(line_value(*report, "stderr.console") == "1",
                  "child standard error is a real ConPTY console");
        }
        check(wait_for_stopped(first_session, 5000), "first session reports completion after the child exits");
        const auto first_screen = first_session.screen_text();
        check(first_screen && first_screen->find("T02-final-marker") != std::string::npos,
              "first session drained the child's tail output before completion");
    }

    if (second_result) {
        auto second_session = std::move(*second_result);
        const auto report = wait_for_report(second.report, 10000);
        check(report.has_value(), "second report written by the real console child");
        if (report) {
            check(line_value(*report, "arg1") == "second", "second session keeps its own arguments");
            check(line_value(*report, "arg2") == "two words", "second session keeps its own spaced argument");
            check(line_value(*report, "env.TERMOLE_PROBE_VALUE") == "second value",
                  "second session keeps its own environment override");
            check(line_value(*report, "cwd") == utf8(second.directory.wstring()),
                  "second session keeps its own working directory");
        }
        check(wait_for_stopped(second_session, 5000), "second session reports completion after the child exits");
        const auto second_screen = second_session.screen_text();
        check(second_screen && second_screen->find("T02-final-marker") != std::string::npos,
              "second session drained the child's tail output before completion");
    }

    std::array<wchar_t, 32768> current{};
    const DWORD current_length =
        ::GetEnvironmentVariableW(L"TERMOLE_PROBE_VALUE", current.data(), static_cast<DWORD>(current.size()));
    check(current_length > 0 && std::wstring(current.data(), current_length) == caller_value,
          "the caller process environment was not modified by session creation");
    check(::GetEnvironmentVariableW(L"TERMOLE_PROBE_EXTRA", current.data(),
                                     static_cast<DWORD>(current.size())) == 0,
          "child-only environment entries do not leak into the caller");

    termole::Session::Options missing_program;
    missing_program.program = (first.directory / L"missing-program.exe").wstring();
    const auto missing_result = termole::Session::create(missing_program);
    check(!missing_result && missing_result.error().status() == TERMOLE_STATUS_NOT_FOUND,
          "missing program returns NOT_FOUND");

    termole::Session::Options missing_directory = first_options;
    missing_directory.working_directory = (first.directory / L"missing-directory").wstring();
    const auto directory_result = termole::Session::create(missing_directory);
    check(!directory_result && directory_result.error().status() == TERMOLE_STATUS_INVALID_ARGUMENT,
          "missing working directory returns INVALID_ARGUMENT");

    termole::Session::Options bad_environment = first_options;
    bad_environment.environment = { L"NO_EQUALS" };
    const auto environment_result = termole::Session::create(bad_environment);
    check(!environment_result && environment_result.error().status() == TERMOLE_STATUS_INVALID_ARGUMENT,
          "invalid environment entry returns INVALID_ARGUMENT");

    termole_session_options raw = TERMOLE_SESSION_OPTIONS_INIT;
    raw.argument_count = 1;
    termole_session* raw_session = nullptr;
    check(termole_session_create(&raw, &raw_session) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "argument count without an argument array is rejected");

    remove_probe(first);
    remove_probe(second);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T02 host exact argv/cwd/environment\n";
    return 0;
}
