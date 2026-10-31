#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string wide_to_utf8(std::wstring_view text)
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

std::wstring environment_value(const wchar_t* name)
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length =
        ::GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

bool append_line(std::string& output, std::string_view line)
{
    output.append(line);
    output.push_back('\n');
    return true;
}

bool write_report(const std::wstring& path, const std::string& text)
{
    const HANDLE file = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!::WriteFile(file, text.data() + offset, static_cast<DWORD>(text.size() - offset),
                         &written, nullptr) ||
            written == 0) {
            ::CloseHandle(file);
            return false;
        }
        offset += written;
    }
    ::CloseHandle(file);
    return true;
}

bool write_stdout(const std::string& text)
{
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!::WriteFile(output, text.data() + offset, static_cast<DWORD>(text.size() - offset),
                         &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    ::SetConsoleOutputCP(CP_UTF8);
    ::SetConsoleCP(CP_UTF8);
    int forced_exit_code = 0;
    bool copy_stdin = false;
    bool sleep_mode = false;
    DWORD sleep_ms = 0;
    DWORD linger_ms = 0;
    for (int index = 1; index < argc; ++index) {
        const std::wstring mode = argv[index];
        if (mode == L"--exit-code" && index + 1 < argc) {
            forced_exit_code = _wtoi(argv[++index]);
            continue;
        }
        if (mode == L"--copy-stdin") {
            copy_stdin = true;
            continue;
        }
        if (mode == L"--read-one") {
            // A consumer that closes the pipeline after one buffer, used to
            // verify --stdio ends cleanly when the caller stops reading.
            char buffer[4096];
            DWORD read = 0;
            ::ReadFile(::GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read, nullptr);
            return 0;
        }
        if (mode == L"--linger-ms" && index + 1 < argc) {
            linger_ms = static_cast<DWORD>(_wtoi(argv[++index]));
            continue;
        }
        if (mode == L"--sleep-ms" && index + 1 < argc) {
            sleep_mode = true;
            sleep_ms = static_cast<DWORD>(_wtoi(argv[++index]));
            continue;
        }
        if (mode == L"--lines" && index + 1 < argc) {
            const int count = _wtoi(argv[++index]);
            for (int line = 0; line < count; ++line) {
                char text[32]{};
                std::snprintf(text, sizeof(text), "line-%04d\r\n", line);
                if (!write_stdout(text)) return 3;
            }
            return 0;
        }
        if (mode == L"--delayed-lines") {
            if (!write_stdout("before-detach\r\n")) return 3;
            ::Sleep(400);
            if (!write_stdout("after-detach\r\n")) return 3;
            return 0;
        }
        if (mode == L"--altscreen") {
            if (!write_stdout("MAIN-SCREEN\r\n")) return 3;
            if (!write_stdout("\x1b[?1049h\x1b[2J\x1b[H" "ALT-SCREEN")) return 3;
            ::Sleep(500);
            if (!write_stdout("\x1b[?1049l" "RESTORED\r\n")) return 3;
            return 0;
        }
        if (mode == L"--unicode") {
            const std::string script =
                "\xE4\xB8\xAD" "\x1b[1;3H" "A\r\n"
                "e\xCC\x81" "X\r\n"
                "\xF0\x9F\x90\xAD" "\x1b[3;3H" "B\r\n"
                "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
                "\xE2\x80\x8D\xF0\x9F\x91\xA6" "\r\n";
            return write_stdout(script) ? 0 : 3;
        }
        if (mode == L"--events") {
            const std::string script = "\x1b]0;Event Title\a\a" "event-tail\r\n";
            return write_stdout(script) ? 7 : 3;
        }
        if (mode == L"--flood" && index + 1 < argc) {
            const int count = _wtoi(argv[++index]);
            for (int item = 0; item < count; ++item) {
                const std::string script = "\x1b]0;Flood " + std::to_string(item) + "\x07\x07";
                if (!write_stdout(script)) return 3;
            }
            return 0;
        }
    }

    if (sleep_mode) ::Sleep(sleep_ms);

    if (copy_stdin) {
        // The --stdio data channel must carry bytes unchanged; this mode makes
        // that observable from a real pipeline.
        const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
        char chunk[8192];
        for (;;) {
            DWORD read = 0;
            if (!::ReadFile(input, chunk, sizeof(chunk), &read, nullptr)) {
                if (::GetLastError() == ERROR_BROKEN_PIPE) break;
                return 3;
            }
            if (read == 0) break;
            if (!write_stdout(std::string(chunk, read))) return 3;
        }
        return forced_exit_code;
    }

    if (sleep_mode) return forced_exit_code;

    const std::wstring report_path = environment_value(L"TERMOLE_PROBE_REPORT");
    if (report_path.empty()) return 2;

    std::string report;
    append_line(report, "argc=" + std::to_string(argc));
    for (int index = 0; index < argc; ++index) {
        const std::string value = wide_to_utf8(argv[index]);
        append_line(report, "arg" + std::to_string(index) + "len=" + std::to_string(value.size()));
        append_line(report, "arg" + std::to_string(index) + "=" + value);
    }

    wchar_t cwd[32768]{};
    const DWORD cwd_length = ::GetCurrentDirectoryW(static_cast<DWORD>(std::size(cwd)), cwd);
    append_line(report, "cwd=" + wide_to_utf8(std::wstring(cwd, cwd_length)));

    append_line(report,
                "env.TERMOLE_PROBE_VALUE=" + wide_to_utf8(environment_value(L"TERMOLE_PROBE_VALUE")));
    append_line(report,
                "env.TERMOLE_PROBE_EXTRA=" + wide_to_utf8(environment_value(L"TERMOLE_PROBE_EXTRA")));
    append_line(report,
                "env.TERMOLE_CALLER_SENTINEL=" +
                    wide_to_utf8(environment_value(L"TERMOLE_CALLER_SENTINEL")));
    append_line(report, "env.SystemRoot=" + wide_to_utf8(environment_value(L"SystemRoot")));

    CONSOLE_SCREEN_BUFFER_INFO screen{};
    if (::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &screen)) {
        append_line(report, "console.cols=" +
            std::to_string(screen.srWindow.Right - screen.srWindow.Left + 1));
        append_line(report, "console.rows=" +
            std::to_string(screen.srWindow.Bottom - screen.srWindow.Top + 1));
    }

    DWORD mode = 0;
    const bool console = ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode) != FALSE;
    append_line(report, std::string("stdout.console=") + (console ? "1" : "0"));
    const bool stdin_console = ::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode) != FALSE;
    append_line(report, std::string("stdin.console=") + (stdin_console ? "1" : "0"));
    const bool stderr_console = ::GetConsoleMode(::GetStdHandle(STD_ERROR_HANDLE), &mode) != FALSE;
    append_line(report, std::string("stderr.console=") + (stderr_console ? "1" : "0"));

    if (!write_report(report_path, report)) return 3;

    // Final output written immediately before exit; the session must drain it
    // before reporting that it stopped.
    const char marker[] = "T02-final-marker\r\n";
    DWORD marker_written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), marker,
                static_cast<DWORD>(sizeof(marker) - 1), &marker_written, nullptr);
    if (linger_ms != 0) ::Sleep(linger_ms);
    return forced_exit_code;
}
