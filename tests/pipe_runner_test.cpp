// T25: the pipe runner hands exact UTF-8 bytes to a real shell command,
// discards command output, ignores its exit code, and diagnoses a shell that
// cannot start.

#include "common/pipe.h"

#include <windows.h>

#include <array>
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

std::filesystem::path module_directory()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

bool wait_for_file(const std::filesystem::path& path, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string hex_encode(std::string_view bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0xF]);
    }
    return out;
}

} // namespace

int wmain()
{
    const auto helper = module_directory() / L"pipe-helper.exe";
    check(std::filesystem::exists(helper), "pipe helper is next to the test executable");

    const auto root = std::filesystem::temp_directory_path() /
        ("termole-t25-runner-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    check(!error, "test artifact directory created");

    const auto output = root / L"visible.txt";
    const std::wstring command = L"call \"" + helper.wstring() + L"\" --sink \"" +
        output.wstring() + L"\" --exit-code 7";

    // The payload covers UTF-8, CRLF, a missing trailing newline and an
    // embedded NUL. The command must receive every byte unchanged.
    std::string text = "alpha\r\n\xE4\xB8\xAD\xE6\x96\x87\nno-final-newline";
    text.push_back('\0');
    text += "after-nul";

    termole::app::PipeRunner runner;
    std::wstring launch_error;
    check(runner.launch(command, text, launch_error), "pipe command launched through the shell");
    if (!launch_error.empty()) std::wcerr << L"launch error: " << launch_error << L'\n';

    check(wait_for_file(std::filesystem::path(output.wstring() + L".done"), 10000),
          "the shell command ran and closed its stdin");
    const std::string received = read_bytes(output);
    if (received != text) {
        std::cerr << "payload mismatch: expected " << text.size() << " bytes, got "
                  << received.size() << " bytes\n";
        std::cerr << "expected hex: " << hex_encode(text) << "\n";
        std::cerr << "actual hex:   " << hex_encode(received) << "\n";
    }
    check(received == text,
          "the command received the exact UTF-8 payload (including NUL)");

    const auto empty_output = root / L"empty.txt";
    const std::wstring empty_command =
        L"call \"" + helper.wstring() + L"\" --sink \"" + empty_output.wstring() + L"\"";
    std::wstring empty_error;
    check(runner.launch(empty_command, std::string{ }, empty_error),
          "an empty pipe text still runs the command");
    check(wait_for_file(std::filesystem::path(empty_output.wstring() + L".done"), 10000),
          "the command sees EOF immediately for empty text");
    check(read_bytes(empty_output).empty(), "empty text delivers zero bytes");

    termole::app::PipeRunner missing_shell(L"Z:\\termole-missing\\cmd.exe");
    std::wstring failure;
    check(!missing_shell.launch(L"echo hello", "payload", failure),
          "a shell that cannot start reports failure");
    check(!failure.empty(), "the start failure leaves a diagnostic");
    check(failure.find(L"could not start") != std::wstring::npos,
          "the diagnostic names the start failure");

    std::filesystem::remove_all(root, error);
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T25 pipe runner\n";
    return 0;
}
