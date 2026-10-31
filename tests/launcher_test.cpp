// T26 launcher acceptance: normal mode returns immediately, -e/--stdio wait
// and forward the program status, the stdio data channel carries bytes
// unchanged, and a console allocated for the launcher is not left behind.

#include <windows.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

bool write_file(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    return output.good();
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

struct Process {
    PROCESS_INFORMATION info{};
    bool started = false;
};

Process spawn(const std::wstring& executable, const std::wstring& arguments, BOOL inherit,
              DWORD flags, STARTUPINFOW* startup)
{
    Process process;
    std::wstring command = quote(executable) + L" " + arguments;
    STARTUPINFOW local{};
    if (!startup) {
        local.cb = sizeof(local);
        startup = &local;
    }
    process.started = ::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, inherit,
                                       flags, nullptr, nullptr, startup, &process.info) != FALSE;
    if (process.started) ::CloseHandle(process.info.hThread);
    return process;
}

DWORD process_exit_code(Process& process)
{
    DWORD code = 0;
    ::GetExitCodeProcess(process.info.hProcess, &code);
    return code;
}

DWORD wait_for(Process& process, DWORD timeout_ms)
{
    const DWORD result = ::WaitForSingleObject(process.info.hProcess, timeout_ms);
    return result == WAIT_OBJECT_0 ? process_exit_code(process) : 0xFFFFFFFFu;
}

void close(Process& process)
{
    if (process.started) ::CloseHandle(process.info.hProcess);
    process.started = false;
}

HWND wait_for_window(const std::wstring& title, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        const HWND window = ::FindWindowW(nullptr, title.c_str());
        if (window) return window;
        ::Sleep(50);
    }
    return nullptr;
}

struct WindowPixels {
    bool captured = false;
    size_t colors = 0;
    size_t dark = 0;
    size_t bright = 0;
};

bool write_bitmap(const std::filesystem::path& path, const void* pixels, int width, int height)
{
    const DWORD image_size = static_cast<DWORD>(width) * static_cast<DWORD>(height) * 4;
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;  // "BM"
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + image_size;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -height;  // top-down
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;
    info.biSizeImage = image_size;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&file), sizeof(file));
    output.write(reinterpret_cast<const char*>(&info), sizeof(info));
    output.write(static_cast<const char*>(pixels), image_size);
    return output.good();
}

// PrintWindow-level evidence that the real window content is rendered: a blank
// window has one colour, a rendered terminal has both background and glyph
// colours. A weak capture is kept as a BMP next to the test artifacts.
WindowPixels capture_window_pixels(HWND window, int width, int height,
                                   const std::filesystem::path& failure_bitmap)
{
    WindowPixels result;
    HDC screen = ::GetDC(nullptr);
    HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP bitmap =
        ::CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap && bits) {
        const HGDIOBJ previous = ::SelectObject(memory, bitmap);
        result.captured = ::PrintWindow(window, memory, PW_RENDERFULLCONTENT) != FALSE;
        const auto* pixels = static_cast<const BYTE*>(bits);
        std::vector<bool> seen(1u << 24, false);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const BYTE* pixel = pixels + (static_cast<size_t>(y) * width + x) * 4;
                const uint32_t color = (static_cast<uint32_t>(pixel[2]) << 16) |
                    (static_cast<uint32_t>(pixel[1]) << 8) | pixel[0];
                if (!seen[color]) {
                    seen[color] = true;
                    ++result.colors;
                }
                const int luma = (pixel[0] * 30 + pixel[1] * 59 + pixel[2] * 11) / 100;
                if (luma < 60) ++result.dark;
                if (luma > 180) ++result.bright;
            }
        }
        if (result.colors < 8 || result.dark <= 100 || result.bright <= 100) {
            (void)write_bitmap(failure_bitmap, pixels, width, height);
        }
        ::SelectObject(memory, previous);
        ::DeleteObject(bitmap);
    }
    ::DeleteDC(memory);
    ::ReleaseDC(nullptr, screen);
    return result;
}

bool wait_for_file(const std::filesystem::path& path, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) {
            if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        }
        ::Sleep(25);
    }
    return false;
}

void set_report_env(const std::filesystem::path& report)
{
    ::SetEnvironmentVariableW(L"TERMOLE_PROBE_REPORT", report.c_str());
}

void clear_report_env()
{
    ::SetEnvironmentVariableW(L"TERMOLE_PROBE_REPORT", nullptr);
}

HANDLE pipe_end(HANDLE pipe[2], bool read_end, bool inheritable)
{
    const HANDLE handle = read_end ? pipe[0] : pipe[1];
    ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, inheritable ? HANDLE_FLAG_INHERIT : 0);
    return handle;
}

std::string console_probe_report(DWORD process_id)
{
    // Re-run ourselves in probe mode so this process's own console stays intact.
    std::array<wchar_t, 32768> module{};
    const DWORD length =
        ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0) return {};
    const auto report = std::filesystem::temp_directory_path() /
        ("termole-t26-console-" + std::to_string(::GetCurrentProcessId()) + ".txt");
    std::filesystem::remove(report);
    std::wstring arguments = L"--console-probe " + std::to_wstring(process_id) + L" " +
        quote(report.wstring());
    Process probe = spawn(std::wstring(module.data(), length), arguments, FALSE, 0, nullptr);
    if (!probe.started) return {};
    const DWORD waited = wait_for(probe, 10000);
    close(probe);
    if (waited == 0xFFFFFFFFu) return {};
    return read_file(report);
}

int run_console_probe(DWORD process_id, const std::filesystem::path& report)
{
    ::FreeConsole();
    std::string result;
    if (!::AttachConsole(process_id)) {
        const DWORD error = ::GetLastError();
        result = error == ERROR_INVALID_HANDLE || error == ERROR_ACCESS_DENIED
            ? "console=none"
            : "console=attach-failed:" + std::to_string(error);
    } else {
        const HWND window = ::GetConsoleWindow();
        result = window ? std::string("console=attached visible=") +
                              (::IsWindowVisible(window) ? "1" : "0")
                        : "console=attached visible=0";
        ::FreeConsole();
    }
    return write_file(report, result) ? 0 : 1;
}

int run_shared_console_case(const std::wstring& app, const std::wstring& helper,
                            const std::filesystem::path& report)
{
    ::FreeConsole();
    if (!::AllocConsole()) {
        // No window station/console available in this environment; the shared
        // console assertion cannot be observed here.
        return write_file(report, "console=unavailable") ? 0 : 1;
    }
    HWND window = nullptr;
    for (int attempt = 0; attempt < 40; ++attempt) {
        window = ::GetConsoleWindow();
        if (window && ::IsWindowVisible(window)) break;
        ::Sleep(50);
    }
    const bool visible_before = window && ::IsWindowVisible(window);
    Process launcher = spawn(app, L"-e " + quote(helper) + L" --sleep-ms 1200", FALSE, 0, nullptr);
    if (!launcher.started) {
        ::FreeConsole();
        return write_file(report, "launcher=start-failed") ? 0 : 1;
    }
    ::Sleep(600);
    const HWND current = ::GetConsoleWindow();
    const bool visible_after = current && ::IsWindowVisible(current);
    const DWORD code = wait_for(launcher, 10000);
    close(launcher);
    ::FreeConsole();
    std::string result;
    if (!visible_before) {
        // Some console hosts (Windows Terminal) expose a hidden pseudo window
        // even though the console exists; visibility cannot be judged there.
        result = "console=no-visible-initial";
    } else {
        result = "visible_before=1 visible_after=" +
            std::to_string(visible_after ? 1 : 0) + " exit=" + std::to_string(code);
    }
    return write_file(report, result) ? 0 : 1;
}

// Feeds payload into the child stdin pipe from a thread so large payloads do
// not fill the pipe before the child starts reading.
void feed_pipe(HANDLE write_end, std::string payload)
{
    size_t offset = 0;
    while (offset < payload.size()) {
        DWORD written = 0;
        if (!::WriteFile(write_end, payload.data() + offset,
                         static_cast<DWORD>(payload.size() - offset), &written, nullptr)) {
            break;
        }
        offset += written;
    }
    ::CloseHandle(write_end);
}

std::string drain_pipe(HANDLE read_end)
{
    std::string output;
    char buffer[8192];
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        output.append(buffer, read);
    }
    ::CloseHandle(read_end);
    return output;
}

struct Scenario {
    std::wstring app;
    std::wstring helper;
    std::filesystem::path root;
};

void normal_mode_returns_immediately(const Scenario& scenario)
{
    const std::wstring title = L"T26 normal " + std::to_wstring(::GetCurrentProcessId());
    const auto report = scenario.root / L"normal-report.txt";
    std::filesystem::remove(report);
    set_report_env(report);
    const auto start = std::chrono::steady_clock::now();
    Process launcher = spawn(scenario.app,
                             L"--title " + quote(title) + L" -- " + quote(scenario.helper) +
                                 L" alpha \"a b\" --linger-ms 3500",
                             FALSE, 0, nullptr);
    check(launcher.started, "normal mode: launcher starts");
    if (!launcher.started) {
        clear_report_env();
        return;
    }
    const DWORD code = wait_for(launcher, 10000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    close(launcher);
    check(code == 0, "normal mode: launcher returns 0");
    check(elapsed < 2500, "normal mode: launcher returns without waiting for the window");
    check(wait_for_file(report, 10000), "normal mode: the GUI role runs the target program");
    const std::string content = read_file(report);
    check(line_value(content, "arg1") == "alpha", "normal mode: launcher preserved arguments");
    check(line_value(content, "arg2") == "a b", "normal mode: launcher preserved spaced arguments");
    const HWND window = wait_for_window(title, 5000);
    check(window != nullptr, "normal mode: the target window exists after the launcher returned");
    if (window) {
        ::Sleep(1200);
        const WindowPixels pixels =
            capture_window_pixels(window, 1000, 640, scenario.root / L"window-capture.bmp");
        check(pixels.captured, "normal mode: the real window can be captured");
        check(pixels.colors >= 8 && pixels.dark > 100 && pixels.bright > 100,
              "normal mode: the window shows rendered text and background pixels");
        ::PostMessageW(window, WM_CLOSE, 0, 0);
        for (int attempt = 0; attempt < 100 && ::IsWindow(window); ++attempt) ::Sleep(50);
    }
    clear_report_env();
}

void execute_mode_waits(const Scenario& scenario)
{
    const auto report = scenario.root / L"execute-report.txt";
    std::filesystem::remove(report);
    set_report_env(report);
    const auto start = std::chrono::steady_clock::now();
    Process launcher = spawn(scenario.app,
                             L"-e " + quote(scenario.helper) + L" --sleep-ms 1200 --exit-code 4",
                             FALSE, 0, nullptr);
    if (!launcher.started) {
        check(false, "-e: launcher starts");
        clear_report_env();
        return;
    }
    const DWORD code = wait_for(launcher, 15000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    close(launcher);
    check(code == 4, "-e: launcher forwards the program exit code");
    check(elapsed >= 1000, "-e: launcher waits for the program");

    std::filesystem::remove(report);
    // The caller's stdin is a real pipe with data: -e must still give the child
    // the ConPTY, not this pipe.
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE stdin_pipe[2]{};
    ::CreatePipe(&stdin_pipe[0], &stdin_pipe[1], &security, 0);
    pipe_end(stdin_pipe, true, true);
    pipe_end(stdin_pipe, false, false);
    const char caller_data[] = "caller-pipe-data\r\n";
    DWORD caller_written = 0;
    ::WriteFile(stdin_pipe[1], caller_data, sizeof(caller_data) - 1, &caller_written, nullptr);
    ::CloseHandle(stdin_pipe[1]);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_pipe[0];
    startup.hStdOutput = INVALID_HANDLE_VALUE;
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    Process streams = spawn(scenario.app, L"-e " + quote(scenario.helper), TRUE, 0, &startup);
    ::CloseHandle(stdin_pipe[0]);
    if (!streams.started) {
        check(false, "-e: stream probe starts");
        clear_report_env();
        return;
    }
    const DWORD stream_code = wait_for(streams, 15000);
    close(streams);
    check(stream_code == 0, "-e: stream probe exits 0");
    check(wait_for_file(report, 5000), "-e: stream probe wrote a report");
    const std::string content = read_file(report);
    check(line_value(content, "stdin.console") == "1",
          "-e: child stdin stays on the ConPTY, not on caller pipes");
    check(line_value(content, "stdout.console") == "1", "-e: child stdout stays on the ConPTY");
    check(line_value(content, "stderr.console") == "1", "-e: child stderr stays on the ConPTY");
    clear_report_env();}

void stdio_forwards_bytes(const Scenario& scenario)
{
    std::string payload;
    payload += "first line\r\n";
    payload.push_back('\0');
    payload += "\xE4\xB8\xAD\xE6\x96\x87 unicode\r\n";
    for (int index = 0; index < 4000; ++index) payload += "payload-" + std::to_string(index) + "\n";

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE stdin_pipe[2]{};
    HANDLE stdout_pipe[2]{};
    check(::CreatePipe(&stdin_pipe[0], &stdin_pipe[1], &security, 0) != FALSE,
          "--stdio: stdin pipe created");
    check(::CreatePipe(&stdout_pipe[0], &stdout_pipe[1], &security, 0) != FALSE,
          "--stdio: stdout pipe created");
    const HANDLE child_input = pipe_end(stdin_pipe, true, true);
    const HANDLE child_output = pipe_end(stdout_pipe, false, true);
    pipe_end(stdin_pipe, false, false);
    pipe_end(stdout_pipe, true, false);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input;
    startup.hStdOutput = child_output;
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);

    Process launcher = spawn(scenario.app,
                             L"--stdio " + quote(scenario.helper) +
                                 L" --copy-stdin --exit-code 9",
                             TRUE, 0, &startup);
    ::CloseHandle(stdin_pipe[0]);
    ::CloseHandle(stdout_pipe[1]);
    check(launcher.started, "--stdio: launcher starts");
    if (!launcher.started) return;

    std::thread feeder([handle = stdin_pipe[1], payload]() { feed_pipe(handle, payload); });
    std::string output = drain_pipe(stdout_pipe[0]);
    feeder.join();
    const DWORD code = wait_for(launcher, 20000);
    close(launcher);
    check(code == 9, "--stdio: launcher forwards the program exit code");
    check(output == payload, "--stdio: bytes arrive unchanged and nothing else is added");
}

void stdio_stream_probe(const Scenario& scenario)
{
    const auto report = scenario.root / L"stdio-report.txt";
    std::filesystem::remove(report);
    set_report_env(report);
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE stdin_pipe[2]{};
    HANDLE stdout_pipe[2]{};
    ::CreatePipe(&stdin_pipe[0], &stdin_pipe[1], &security, 0);
    ::CreatePipe(&stdout_pipe[0], &stdout_pipe[1], &security, 0);
    pipe_end(stdin_pipe, true, true);
    pipe_end(stdin_pipe, false, false);
    pipe_end(stdout_pipe, false, true);
    pipe_end(stdout_pipe, true, false);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_pipe[0];
    startup.hStdOutput = stdout_pipe[1];
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    Process launcher = spawn(scenario.app, L"--stdio " + quote(scenario.helper), TRUE, 0, &startup);
    ::CloseHandle(stdin_pipe[0]);
    ::CloseHandle(stdin_pipe[1]);
    ::CloseHandle(stdout_pipe[1]);
    if (!launcher.started) {
        ::CloseHandle(stdout_pipe[0]);
        check(false, "--stdio: stream probe starts");
        clear_report_env();
        return;
    }
    const std::string output = drain_pipe(stdout_pipe[0]);
    const DWORD code = wait_for(launcher, 15000);
    close(launcher);
    (void)output;
    check(code == 0, "--stdio: stream probe exits 0");
    check(wait_for_file(report, 5000), "--stdio: stream probe wrote a report");
    const std::string content = read_file(report);
    check(line_value(content, "stdin.console") == "0",
          "--stdio: child stdin is the caller data channel");
    check(line_value(content, "stdout.console") == "0",
          "--stdio: child stdout is the caller data channel");
    clear_report_env();
}

void stdio_requires_redirection(const Scenario& scenario)
{
    // CREATE_NEW_CONSOLE gives the launcher real console stdin/stdout, which
    // --stdio must reject with the usage status instead of reading the keyboard.
    Process launcher = spawn(scenario.app, L"--stdio " + quote(scenario.helper) + L" --lines 1",
                             FALSE, CREATE_NEW_CONSOLE, nullptr);
    check(launcher.started, "--stdio console check: launcher starts");
    if (!launcher.started) return;
    const DWORD code = wait_for(launcher, 15000);
    close(launcher);
    check(code == 2, "--stdio: console stdin/stdout returns usage error 2");
    // Let the console host finish tearing down the console before the next
    // CREATE_NEW_CONSOLE scenario starts.
    ::Sleep(400);
}

void allocated_console_is_hidden(const Scenario& scenario)
{
    ::Sleep(300);
    // An exclusive console (Explorer/new-console launch) must not be left as a
    // blank visible window while the GUI role runs.
    Process launcher = spawn(
        scenario.app, L"-e " + quote(scenario.helper) + L" --sleep-ms 2000", FALSE,
        CREATE_NEW_CONSOLE, nullptr);
    check(launcher.started, "console hiding: launcher starts with a new console");
    if (!launcher.started) return;
    const std::string probe = console_probe_report(launcher.info.dwProcessId);
    check(probe.find("console=none") != std::string::npos ||
              probe.find("visible=0") != std::string::npos,
          "console hiding: the launcher's own console is hidden or gone");
    const DWORD code = wait_for(launcher, 15000);
    close(launcher);
    if (code != 0) {
        std::cerr << "console hiding: launcher exit code was " << code << '\n';
    }
    check(code == 0, "console hiding: -e still returns the program status");
}

void shared_console_is_preserved(const Scenario& scenario)
{
    std::array<wchar_t, 32768> module{};
    const DWORD length =
        ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0) {
        check(false, "console preservation: test executable path");
        return;
    }
    const auto report = scenario.root / L"shared-console.txt";
    std::filesystem::remove(report);
    Process parent = spawn(std::wstring(module.data(), length),
                           L"--shared-console " + quote(scenario.app) + L" " +
                               quote(scenario.helper) + L" " + quote(report.wstring()),
                           FALSE, 0, nullptr);
    if (!parent.started) {
        check(false, "console preservation: parent starts");
        return;
    }
    const DWORD code = wait_for(parent, 20000);
    close(parent);
    const std::string content = read_file(report);
    check(code == 0, "console preservation: parent exits 0");
    if (content.find("console=unavailable") != std::string::npos ||
        content.find("console=no-visible-initial") != std::string::npos) {
        std::cout << "SKIP: no visible console available for the shared-console probe\n";
        return;
    }
    check(content.find("visible_before=1") != std::string::npos,
          "console preservation: caller console is visible before launch");
    check(content.find("visible_after=1") != std::string::npos,
          "console preservation: launcher does not hide the caller console");
}

void canceling_the_window_returns_130(const Scenario& scenario)
{
    const std::wstring title = L"T26 cancel " + std::to_wstring(::GetCurrentProcessId());
    Process launcher = spawn(scenario.app,
                             L"--title " + quote(title) + L" -e " + quote(scenario.helper) +
                                 L" --sleep-ms 8000",
                             FALSE, 0, nullptr);
    check(launcher.started, "cancel: launcher starts");
    if (!launcher.started) return;
    const HWND window = wait_for_window(title, 15000);
    check(window != nullptr, "cancel: the waiting window exists");
    if (window) ::PostMessageW(window, WM_CLOSE, 0, 0);
    const DWORD code = wait_for(launcher, 15000);
    close(launcher);
    check(code == 130, "cancel: closing the window before the program exits returns 130");
}

void closed_output_pipe_ends_cleanly(const Scenario& scenario)
{
    const auto input_path = scenario.root / L"closed-output-input.bin";
    std::string payload(256 * 1024, 'x');
    write_file(input_path, payload);

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE output_pipe[2]{};
    ::CreatePipe(&output_pipe[0], &output_pipe[1], &security, 0);
    ::CloseHandle(output_pipe[0]);  // the consumer is already gone
    const HANDLE input = ::CreateFileW(input_path.c_str(), GENERIC_READ, FILE_SHARE_READ, &security,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output_pipe[1];
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    Process launcher = spawn(scenario.app, L"--stdio " + quote(scenario.helper) + L" --copy-stdin",
                             TRUE, 0, &startup);
    ::CloseHandle(input);
    ::CloseHandle(output_pipe[1]);
    check(launcher.started, "--stdio early close: launcher starts");
    if (!launcher.started) return;
    const DWORD code = wait_for(launcher, 15000);
    close(launcher);
    check(code != 0xFFFFFFFFu, "--stdio early close: launcher does not block forever");
    check(code != 0, "--stdio early close: the program's broken-pipe status is forwarded");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    ::SetConsoleOutputCP(CP_UTF8);
    if (argc >= 4 && std::wstring_view(argv[1]) == L"--console-probe") {
        return run_console_probe(static_cast<DWORD>(_wtoi(argv[2])), argv[3]);
    }
    if (argc >= 5 && std::wstring_view(argv[1]) == L"--shared-console") {
        return run_shared_console_case(argv[2], argv[3], argv[4]);
    }
    if (argc < 4) {
        std::cerr << "usage: launcher_test <termole.exe> <helper> <root>\n";
        return 2;
    }
    Scenario scenario;
    scenario.app = argv[1];
    scenario.helper = argv[2];
    scenario.root = argv[3];
    std::error_code error;
    std::filesystem::create_directories(scenario.root, error);

    normal_mode_returns_immediately(scenario);
    execute_mode_waits(scenario);
    stdio_forwards_bytes(scenario);
    stdio_stream_probe(scenario);
    stdio_requires_redirection(scenario);
    allocated_console_is_hidden(scenario);
    shared_console_is_preserved(scenario);
    canceling_the_window_returns_130(scenario);
    closed_output_pipe_ends_cleanly(scenario);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T26 launcher normal/execute/stdio/console behavior\n";
    return 0;
}
