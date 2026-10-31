// Controlled target for the T25 pipe acceptance (#28). It has two jobs:
//
//   --sink <path> [--exit-code N]
//     Read stdin to <path>; write noise to stdout and stderr; write
//     <path>.done. Exits with N (default 0).
//
//   --content <count> --ready <path> --input-log <path>
//     Print `count` predictable rows without a trailing CRLF, signal <path>,
//     then enable VT input and append every raw byte chunk to <path>. Termole
//     kills the process when the window closes.
//
// The helper is a test probe, not a product dependency.

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

bool write_all(HANDLE handle, std::string_view bytes)
{
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!::WriteFile(handle, bytes.data() + offset,
                         static_cast<DWORD>(bytes.size() - offset), &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool write_file(const std::wstring& path, std::string_view bytes)
{
    const std::wstring temporary = path + L".tmp";
    HANDLE file = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool ok = write_all(file, bytes);
    ::CloseHandle(file);
    if (!ok) return false;
    return ::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

bool write_marker(const std::wstring& path)
{
    return write_file(path, {});
}

int run_sink(const std::wstring& output, int exit_code)
{
    std::string bytes;
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    char buffer[8192];
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        bytes.append(buffer, read);
    }
    if (!write_file(output, bytes)) return 2;

    const HANDLE standard_output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE standard_error = ::GetStdHandle(STD_ERROR_HANDLE);
    (void)write_all(standard_output, "PIPE-SINK-STDOUT-NOISE\r\n");
    (void)write_all(standard_error, "PIPE-SINK-STDERR-NOISE\r\n");
    if (!write_marker(output + L".done")) return 2;
    return exit_code;
}

int run_content(int count, const std::wstring& ready, const std::wstring& input_log, int delay_ms)
{
    if (delay_ms > 0) ::Sleep(static_cast<DWORD>(delay_ms));
    ::SetConsoleOutputCP(CP_UTF8);
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    for (int line = 1; line <= count; ++line) {
        char buffer[64]{};
        const int length = std::snprintf(buffer, sizeof(buffer), "row-%04d-alpha", line);
        if (length <= 0) return 2;
        (void)write_all(output, std::string_view(buffer, static_cast<size_t>(length)));
        if (line != count) (void)write_all(output, "\r\n");
    }
    if (!write_marker(ready)) return 2;

    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    if (!::SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT)) return 3;
    HANDLE log = ::CreateFileW(input_log.c_str(), FILE_APPEND_DATA,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) return 2;
    for (;;) {
        char buffer[256];
        DWORD read = 0;
        if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        (void)write_all(log, std::string_view(buffer, read));
        (void)::FlushFileBuffers(log);
    }
    ::CloseHandle(log);
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring mode = argc > 1 ? argv[1] : L"";
    if (mode == L"--sink" && argc >= 3) {
        int exit_code = 0;
        for (int index = 3; index + 1 < argc; ++index) {
            if (std::wstring(argv[index]) == L"--exit-code") exit_code = _wtoi(argv[index + 1]);
        }
        return run_sink(argv[2], exit_code);
    }
    if (mode == L"--content" && argc >= 3) {
        const int count = _wtoi(argv[2]);
        int delay_ms = 0;
        std::wstring ready;
        std::wstring input_log;
        for (int index = 3; index + 1 < argc; ++index) {
            const std::wstring name = argv[index];
            if (name == L"--ready") {
                ready = argv[++index];
            } else if (name == L"--input-log") {
                input_log = argv[++index];
            } else if (name == L"--delay-ms") {
                delay_ms = _wtoi(argv[++index]);
            }
        }
        if (ready.empty() || input_log.empty() || count <= 0) return 2;
        return run_content(count, ready, input_log, delay_ms);
    }
    return 2;
}
