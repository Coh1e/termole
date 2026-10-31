// T25 end-to-end acceptance (#28): a real termole.exe reads [pipe.*] from a
// config file, triggers the three pipe actions from posted View key messages,
// and hands exact UTF-8 text to real shell commands. The commands' stdout and
// stderr must not reach the terminal or the caller's stdout.

#include <windows.h>

#include <termole/termole.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

std::string read_bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool wait_for_file(const std::filesystem::path& path, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) return true;
        ::Sleep(20);
    }
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

HWND find_window(const std::wstring& title, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const HWND window = ::FindWindowW(nullptr, title.c_str())) return window;
        ::Sleep(30);
    }
    return ::FindWindowW(nullptr, title.c_str());
}

struct FindView {
    HWND view = nullptr;
};

BOOL CALLBACK find_view(HWND window, LPARAM parameter)
{
    auto* find = reinterpret_cast<FindView*>(parameter);
    wchar_t name[64]{};
    if (::GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0) return TRUE;
    if (std::wstring_view(name) == L"TermoleViewWindow") {
        find->view = window;
        return FALSE;
    }
    return TRUE;
}

HWND find_view_window(HWND parent, int timeout_ms)
{
    FindView find;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        ::EnumChildWindows(parent, find_view, reinterpret_cast<LPARAM>(&find));
        if (find.view) return find.view;
        ::Sleep(30);
    }
    return find.view;
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
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + image_size;
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = width;
    info.biHeight = -height;
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

// PrintWindow evidence that the content is really rendered. text/selection
// conclusions still come from the bytes the commands received; the pixels show
// the rows the user sees are not a blank surface.
WindowPixels capture_window(HWND window, const std::filesystem::path& failure_bitmap)
{
    WindowPixels result;
    RECT rectangle{};
    if (!::GetWindowRect(window, &rectangle)) return result;
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (width <= 0 || height <= 0) return result;

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
    const HBITMAP bitmap = ::CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
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
        if (result.captured) (void)write_bitmap(failure_bitmap, pixels, width, height);
        ::SelectObject(memory, previous);
        ::DeleteObject(bitmap);
    }
    ::DeleteDC(memory);
    ::ReleaseDC(nullptr, screen);
    return result;
}

std::string expected_line(int index)
{
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "row-%04d-alpha", index);
    return buffer;
}

std::string join(const std::vector<std::string>& lines, size_t first, size_t last)
{
    std::string text;
    for (size_t index = first; index <= last; ++index) {
        if (!text.empty()) text.push_back('\n');
        text += lines[index];
    }
    return text;
}

std::string visible_text(std::string_view text)
{
    std::string result;
    for (const unsigned char value : text) {
        if (value == '\n') {
            result += "\\n";
        } else if (value == '\r') {
            result += "\\r";
        } else if (value >= 0x20 && value < 0x7F) {
            result.push_back(static_cast<char>(value));
        } else {
            char buffer[8]{};
            std::snprintf(buffer, sizeof(buffer), "\\x%02X", value);
            result += buffer;
        }
    }
    return result;
}

void report_difference(const char* name, const std::string& expected, const std::string& actual)
{
    std::cerr << name << " mismatch\n  expected: " << visible_text(expected)
              << "\n  actual:   " << visible_text(actual) << '\n';
}

// The applet window is sized from the requested grid, but the client area can
// round to a different number of cells. Measure the same default font/DPI as
// the app and derive the grid from the real View rect instead of assuming the
// CLI numbers.
struct CellMetrics {
    float width = 0.0f;
    float height = 0.0f;
};

CellMetrics measure_cell_metrics()
{
    CellMetrics result;
    const wchar_t kProbeClass[] = L"TermoleT25ProbeParent";
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ::DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = kProbeClass;
    if (!::RegisterClassW(&window_class) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return result;
    }
    const HWND parent = ::CreateWindowExW(0, kProbeClass, L"t25-probe", WS_OVERLAPPEDWINDOW, 0, 0,
                                          400, 300, nullptr, nullptr,
                                          ::GetModuleHandleW(nullptr), nullptr);
    if (parent) {
        termole::Session::Options session_options;
        session_options.program = L"cmd.exe";
        session_options.arguments = { L"/d", L"/c", L"exit" };
        session_options.columns = 80;
        session_options.rows = 24;
        auto session = termole::Session::create(session_options);
        if (session) {
            auto view = termole::View::create(*session, parent);
            if (view) {
                result.width = view->cell_width();
                result.height = view->cell_height();
            }
            (void)session->terminate();
        }
        ::DestroyWindow(parent);
    }
    ::UnregisterClassW(kProbeClass, ::GetModuleHandleW(nullptr));
    return result;
}

void send_chord(HWND view, UINT key, WPARAM character)
{
    ::SendMessageW(view, WM_KEYDOWN, VK_CONTROL, 0);
    ::SendMessageW(view, WM_KEYDOWN, VK_SHIFT, 0);
    ::SendMessageW(view, WM_KEYDOWN, key, 0);
    ::SendMessageW(view, WM_CHAR, character, 0);
    ::SendMessageW(view, WM_KEYUP, key, 0);
    ::SendMessageW(view, WM_KEYUP, VK_SHIFT, 0);
    ::SendMessageW(view, WM_KEYUP, VK_CONTROL, 0);
}

struct Process {
    PROCESS_INFORMATION info{};
    bool started = false;
};

Process spawn(const std::filesystem::path& executable, const std::wstring& arguments,
              HANDLE standard_input, HANDLE standard_output, HANDLE standard_error)
{
    Process process;
    std::wstring command = L"\"" + executable.wstring() + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = standard_input;
    startup.hStdOutput = standard_output;
    startup.hStdError = standard_error;
    process.started = ::CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                       0, nullptr, nullptr, &startup, &process.info) != FALSE;
    if (process.started) ::CloseHandle(process.info.hThread);
    return process;
}

std::string read_pipe(HANDLE read_end)
{
    std::string bytes;
    char buffer[4096];
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(read_end, buffer, sizeof(buffer), &read, nullptr) || read == 0) break;
        bytes.append(buffer, read);
    }
    return bytes;
}

std::wstring quote(const std::filesystem::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

std::wstring quote(const std::wstring& text)
{
    return L"\"" + text + L"\"";
}

std::string utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0, nullptr,
                                           nullptr);
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                          size, nullptr, nullptr);
    return result;
}

void configure_pipe(std::ofstream& config, const char* action, std::wstring_view key,
                    const std::wstring& command)
{
    config << "\n[pipe." << action << "]\nkey = '" << utf8(key) << "'\ncommand = '"
           << utf8(command) << "'\n";
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (argc < 4) {
        std::cerr << "usage: pipe_e2e_test termole.exe pipe-helper.exe <artifact-dir>\n";
        return 2;
    }
    const std::filesystem::path app = argv[1];
    const std::filesystem::path helper = argv[2];
    const std::filesystem::path root = argv[3];
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    check(!error, "test artifact directory created");

    const auto visible_out = root / L"visible.txt";
    const auto selected_out = root / L"selected.txt";
    const auto scrollback_out = root / L"scrollback.txt";
    const auto config_path = root / L"config.toml";

    // `call` is a cmd.exe built-in: the command cannot run unless Termole uses
    // the Windows shell. The first command exits non-zero on purpose.
    const std::wstring visible_command =
        L"call " + quote(helper) + L" --sink " + quote(visible_out) + L" --exit-code 7";
    const std::wstring selected_command =
        quote(helper) + L" --sink " + quote(selected_out);
    const std::wstring scrollback_command =
        quote(helper) + L" --sink " + quote(scrollback_out);

    {
        std::ofstream config(config_path, std::ios::binary | std::ios::trunc);
        config << "columns = 80\nrows = 10\n";
        configure_pipe(config, "visible", L"ctrl+shift+j", visible_command);
        configure_pipe(config, "selected", L"ctrl+shift+k", selected_command);
        configure_pipe(config, "scrollback", L"ctrl+shift+l", scrollback_command);
    }
    check(std::filesystem::exists(config_path), "pipe configuration written");

    const std::wstring title =
        L"termole-t25-pipe-" + std::to_wstring(::GetCurrentProcessId());
    const auto ready = root / L"content.ready";
    const auto input_log = root / L"input.bin";

    HANDLE output_pipe[2]{};
    HANDLE error_pipe[2]{};
    ::CreatePipe(&output_pipe[0], &output_pipe[1], nullptr, 0);
    ::CreatePipe(&error_pipe[0], &error_pipe[1], nullptr, 0);
    ::SetHandleInformation(output_pipe[0], HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(error_pipe[0], HANDLE_FLAG_INHERIT, 0);
    HANDLE null_input = ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, 0, nullptr);

    const std::wstring arguments = L"--config " + quote(config_path) +
        L" --columns 80 --rows 10 --title " + quote(title) + L" -e " + quote(helper) +
        L" --content 30 --delay-ms 800 --ready " + quote(ready) + L" --input-log " + quote(input_log);
    Process process = spawn(app, arguments, null_input, output_pipe[1], error_pipe[1]);
    ::CloseHandle(output_pipe[1]);
    ::CloseHandle(error_pipe[1]);
    check(process.started, "termole launcher started");
    if (!process.started) {
        std::cerr << "CreateProcess error " << ::GetLastError() << '\n';
        return 1;
    }

    const HWND window = find_window(title, 15000);
    check(window != nullptr, "the termole window became visible");
    const HWND view = window ? find_view_window(window, 5000) : nullptr;
    check(view != nullptr, "the terminal View child window exists");
    check(wait_for_file(ready, 15000), "the controlled program printed its rows");
    ::Sleep(700);

    const WindowPixels pixels = window ? capture_window(window, root / L"window-capture.bmp")
                                        : WindowPixels{};
    check(pixels.captured, "PrintWindow captured the real window");
    check(pixels.colors >= 8 && pixels.dark > 100 && pixels.bright > 100,
          "the captured window has both background and glyph pixels");

    std::vector<std::string> lines;
    for (int index = 1; index <= 30; ++index) lines.push_back(expected_line(index));

    // The window can round the requested grid to a different cell count. The
    // probe View uses the same default font and DPI as the app, so this is the
    // same division the View performs on its client rect.
    const CellMetrics cell = measure_cell_metrics();
    RECT client{};
    if (view) ::GetClientRect(view, &client);
    const int columns = cell.width > 0.0f ? static_cast<int>(client.right / cell.width) : 0;
    const int rows = cell.height > 0.0f ? static_cast<int>(client.bottom / cell.height) : 0;
    check(columns >= 15 && rows > 1 && rows < 30, "the measured terminal grid is usable");

    if (view) {
        send_chord(view, 'J', 0x0A);
        const LPARAM start = MAKELPARAM(0, 0);
        const LPARAM finish = MAKELPARAM(client.right + 100, client.bottom + 100);
        ::SendMessageW(view, WM_LBUTTONDOWN, MK_LBUTTON, start);
        ::SendMessageW(view, WM_MOUSEMOVE, MK_LBUTTON, finish);
        ::SendMessageW(view, WM_LBUTTONUP, 0, finish);
        send_chord(view, 'K', 0x0B);
        send_chord(view, 'L', 0x0C);
        // Ordinary input after the chords must still reach the program.
        ::SendMessageW(view, WM_CHAR, L'Z', 0);
    }

    check(wait_for_file(std::filesystem::path(visible_out.wstring() + L".done"), 15000),
          "pipe-visible ran its command");
    check(wait_for_file(std::filesystem::path(selected_out.wstring() + L".done"), 15000),
          "pipe-selected ran its command");
    check(wait_for_file(std::filesystem::path(scrollback_out.wstring() + L".done"), 15000),
          "pipe-scrollback ran its command");

    const size_t visible_first = static_cast<size_t>(30 - rows);
    const std::string visible_expected = join(lines, visible_first, 29);
    const std::string scrollback_expected = join(lines, 0, visible_first - 1);
    std::string selected_expected;
    for (size_t index = visible_first; index < 30; ++index) {
        if (!selected_expected.empty()) selected_expected.push_back('\n');
        selected_expected += lines[index];
        if (static_cast<size_t>(columns) > lines[index].size()) {
            selected_expected.append(static_cast<size_t>(columns) - lines[index].size(), ' ');
        }
    }

    const std::string visible_actual = read_bytes(visible_out);
    const std::string selected_actual = read_bytes(selected_out);
    const std::string scrollback_actual = read_bytes(scrollback_out);
    if (visible_actual != visible_expected) report_difference("pipe-visible", visible_expected, visible_actual);
    if (selected_actual != selected_expected) report_difference("pipe-selected", selected_expected, selected_actual);
    if (scrollback_actual != scrollback_expected) report_difference("pipe-scrollback", scrollback_expected, scrollback_actual);
    check(visible_actual == visible_expected, "pipe-visible received the exact visible UTF-8 text");
    check(selected_actual == selected_expected, "pipe-selected received the exact selection text");
    check(scrollback_actual == scrollback_expected, "pipe-scrollback received the bounded scrollback text");

    const auto input_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
    while (std::chrono::steady_clock::now() < input_deadline) {
        const std::string log = read_bytes(input_log);
        if (log == "Z") break;
        ::Sleep(30);
    }
    check(read_bytes(input_log) == "Z",
          "a configured chord is consumed and later input still reaches the program");

    check(::IsWindow(window) != FALSE, "a non-zero pipe command exit code does not close the session");

    ::PostMessageW(window, WM_CLOSE, 0, 0);
    const DWORD waited = ::WaitForSingleObject(process.info.hProcess, 15000);
    check(waited == WAIT_OBJECT_0, "the termole process finished so the caller pipes can be read");

    const std::string caller_stdout = read_pipe(output_pipe[0]);
    const std::string caller_stderr = read_pipe(error_pipe[0]);
    check(caller_stdout.empty(), "pipe commands and the terminal do not pollute caller stdout");
    check(caller_stderr.find("PIPE-SINK-STDERR-NOISE") == std::string::npos,
          "pipe command stderr is discarded");

    ::CloseHandle(output_pipe[0]);
    ::CloseHandle(error_pipe[0]);
    if (null_input != INVALID_HANDLE_VALUE) ::CloseHandle(null_input);
    ::CloseHandle(process.info.hProcess);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T25 pipe actions end to end\n";
    return 0;
}
