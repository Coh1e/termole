// #27 pixel-level View/window regression harness.
//
// Two scenarios:
//   alt-screen  a controlled child writes known text into the alternate
//               screen; the View is captured with PrintWindow and must show
//               non-background pixels.
//   fzf         real fzf draws its full-screen UI; the View must show the
//               candidate UI. Part of the default suite since #26 was fixed.
//
// screen_text()/viewport_text()/paint_count() are printed only as supporting
// signals. The pass criterion is the captured pixels.

#include <termole/termole.hpp>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

constexpr int kSkipExitCode = 77;
constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 640;
constexpr long long kMinimumInkPixels = 500;
constexpr double kMinimumInkRatio = 0.0008;
constexpr long long kMinimumBandInkPixels = 50;
constexpr long long kMinimumPointerInkPixels = 10;
// A filled fzf selection highlight has far more strongly-contrasting pixels
// than the same row's glyphs. These coarse values only separate "inverse
// video block" from "plain candidate text"; they are not colour assertions.
constexpr long long kMinimumSelectedStrongPixels = 800;
// Moving the selection changes the same row from plain glyphs to a filled
// inverse-video block. Comparing the two rows' strong-pixel counts avoids a
// per-row absolute threshold that would depend on the candidate's text length.
constexpr long long kSelectionStrongDelta = 500;
constexpr long long kMaximumBlankBandInkPixels = 5;

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

void pump_messages();

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

std::string narrow_ascii(std::wstring_view text)
{
    std::string result;
    result.reserve(text.size());
    for (const wchar_t character : text) result.push_back(static_cast<char>(character));
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
    while (position <= report.size()) {
        const size_t end = report.find('\n', position);
        std::string line = report.substr(
            position, (end == std::string::npos ? report.size() : end) - position);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.compare(0, prefix.size(), prefix) == 0) return line.substr(prefix.size());
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return {};
}

struct Options {
    std::wstring mode;
    std::wstring child;
    std::wstring input_child;
    std::wstring fzf;
    std::wstring app;
    std::wstring powershell;
    std::wstring capture;
    std::filesystem::path artifacts;
};

std::optional<Options> parse_arguments(int argc, wchar_t** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        const auto take = [&](std::wstring& target) {
            if (index + 1 >= argc) return false;
            target = argv[++index];
            return true;
        };
        if (argument == L"--mode") {
            if (!take(options.mode)) return std::nullopt;
        } else if (argument == L"--child") {
            if (!take(options.child)) return std::nullopt;
        } else if (argument == L"--input-child") {
            if (!take(options.input_child)) return std::nullopt;
        } else if (argument == L"--fzf") {
            if (!take(options.fzf)) return std::nullopt;
        } else if (argument == L"--app") {
            if (!take(options.app)) return std::nullopt;
        } else if (argument == L"--powershell") {
            if (!take(options.powershell)) return std::nullopt;
        } else if (argument == L"--capture") {
            if (!take(options.capture)) return std::nullopt;
        } else if (argument == L"--artifacts") {
            std::wstring value;
            if (!take(value)) return std::nullopt;
            options.artifacts = value;
        } else {
            std::wcerr << L"unknown argument: " << argument << L'\n';
            return std::nullopt;
        }
    }
    if (options.mode.empty() || options.artifacts.empty() || options.powershell.empty() ||
        options.capture.empty()) {
        std::cerr << "usage: termole_visual_test "
                     "--mode <alt-screen|fzf|fzf-interaction|fzf-standalone|input-interaction|"
                     "input-standalone> "
                     "--artifacts <dir> --powershell <powershell.exe> "
                     "--capture <capture-window.ps1> "
                     "[--child <alt_screen_child.exe>] [--input-child <t12_input_child.exe>] "
                     "[--fzf <fzf.exe>] [--app <termole.exe>]\n";
        return std::nullopt;
    }
    return options;
}

struct CaptureStats {
    int width = 0;
    int height = 0;
    long long colors = 0;
    long long ink = 0;
    long long band_ink = 0;
    long long strong = 0;
    long long band_strong = 0;
    double ink_ratio = 0.0;
    std::string background;
};

std::optional<CaptureStats> read_stats(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return std::nullopt;
    const std::string text = read_file(path);
    if (text.empty()) return std::nullopt;

    CaptureStats stats;
    stats.width = std::atoi(line_value(text, "width").c_str());
    stats.height = std::atoi(line_value(text, "height").c_str());
    stats.colors = std::atoll(line_value(text, "colors").c_str());
    stats.ink = std::atoll(line_value(text, "inkPixels").c_str());
    stats.band_ink = std::atoll(line_value(text, "bandInkPixels").c_str());
    stats.strong = std::atoll(line_value(text, "strongPixels").c_str());
    stats.band_strong = std::atoll(line_value(text, "bandStrongPixels").c_str());
    stats.ink_ratio = std::atof(line_value(text, "inkRatio").c_str());
    stats.background = line_value(text, "background");
    return stats;
}

bool run_capture_script(const Options& options, HWND window, const std::wstring& name,
                        const std::filesystem::path& artifacts, CaptureStats& stats,
                        int band_top = -1, int band_bottom = -1)
{
    std::error_code error;
    const std::filesystem::path png = artifacts / (name + L".png");
    const std::filesystem::path stats_path = artifacts / (name + L".txt");
    const std::filesystem::path error_path = artifacts / (name + L".err.txt");
    std::filesystem::remove(png, error);
    std::filesystem::remove(stats_path, error);
    std::filesystem::remove(error_path, error);

    RECT rectangle{};
    if (!::GetWindowRect(window, &rectangle)) {
        std::cerr << "FAIL: GetWindowRect failed\n";
        return false;
    }
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;

    std::wstring band;
    if (band_top >= 0 && band_bottom >= 0) {
        band = L" -BandTop " + std::to_wstring(band_top) + L" -BandBottom " +
            std::to_wstring(band_bottom);
    }
    const std::wstring command = quote(options.powershell) +
        L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(options.capture) +
        L" -Hwnd " + std::to_wstring(reinterpret_cast<uintptr_t>(window)) + L" -Width " +
        std::to_wstring(width) + L" -Height " + std::to_wstring(height) + L" -OutputPath " +
        quote(png.wstring()) + L" -StatsPath " + quote(stats_path.wstring()) + band + L" -Quiet";
    std::wstring mutable_command = command;

    // Keep the capture script's diagnostics next to its PNG. Without an
    // inherited stderr the child is a no-console process and a failing script
    // would only report its exit code.
    SECURITY_ATTRIBUTES inheritable{ sizeof(inheritable), nullptr, TRUE };
    HANDLE error_file = ::CreateFileW(error_path.c_str(), GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE null_input = ::CreateFileW(L"NUL", GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable,
                                      OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    if (error_file != INVALID_HANDLE_VALUE && null_input != INVALID_HANDLE_VALUE) {
        startup.dwFlags |= STARTF_USESTDHANDLES;
        startup.hStdInput = null_input;
        startup.hStdOutput = error_file;
        startup.hStdError = error_file;
    }
    PROCESS_INFORMATION process{};
    const BOOL started = ::CreateProcessW(
        options.powershell.c_str(), mutable_command.data(), nullptr, nullptr,
        startup.dwFlags & STARTF_USESTDHANDLES ? TRUE : FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
        &startup, &process);
    if (null_input != INVALID_HANDLE_VALUE) ::CloseHandle(null_input);
    if (error_file != INVALID_HANDLE_VALUE) ::CloseHandle(error_file);
    if (!started) {
        std::cerr << "FAIL: could not start the capture script: " << ::GetLastError() << '\n';
        return false;
    }
    // PrintWindow sends messages to the probe window from the capture process,
    // so this thread must keep pumping while it waits. A plain
    // WaitForSingleObject here deadlocks the cross-process capture.
    DWORD wait_result = WAIT_TIMEOUT;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (;;) {
        const DWORD waited = ::MsgWaitForMultipleObjectsEx(
            1, &process.hProcess, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (waited == WAIT_OBJECT_0) {
            wait_result = WAIT_OBJECT_0;
            break;
        }
        if (waited == WAIT_OBJECT_0 + 1) pump_messages();
        if (std::chrono::steady_clock::now() >= deadline) break;
    }
    DWORD exit_code = 0;
    ::GetExitCodeProcess(process.hProcess, &exit_code);
    if (wait_result != WAIT_OBJECT_0) ::TerminateProcess(process.hProcess, 1);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    if (wait_result != WAIT_OBJECT_0) {
        if (wait_result == WAIT_TIMEOUT) {
            std::cerr << "FAIL: the capture script did not finish in 60 s\n";
        } else {
            std::cerr << "FAIL: waiting for the capture script failed: " << ::GetLastError()
                      << '\n';
        }
        return false;
    }
    if (exit_code != 0) {
        std::cerr << "FAIL: the capture script exited with " << exit_code << '\n';
        std::string diagnostics = read_file(error_path);
        if (diagnostics.size() > 2000) diagnostics = diagnostics.substr(diagnostics.size() - 2000);
        if (!diagnostics.empty()) std::cerr << "capture stderr:\n" << diagnostics << '\n';
        return false;
    }
    std::filesystem::remove(error_path, error);

    const auto parsed = read_stats(stats_path);
    if (!parsed) {
        std::cerr << "FAIL: the capture script did not write " << stats_path.string() << '\n';
        return false;
    }
    stats = *parsed;
    return true;
}

void print_stats(const CaptureStats& stats)
{
    std::cout << "window=" << stats.width << "x" << stats.height << " colors=" << stats.colors
              << " background=" << stats.background << " inkPixels=" << stats.ink
              << " inkRatio=" << stats.ink_ratio << '\n';
}

void check_pixels(const CaptureStats& stats)
{
    check(stats.ink >= kMinimumInkPixels && stats.ink_ratio >= kMinimumInkRatio,
          "the window contains non-background text pixels (>= " +
              std::to_string(kMinimumInkPixels) + " px and >= " +
              std::to_string(kMinimumInkRatio) + " of the window)");
}

struct PixelBand {
    int top = 0;
    int bottom = 0;
};

std::optional<int> find_line_index(std::string_view screen, std::string_view marker)
{
    int index = 0;
    size_t position = 0;
    while (position <= screen.size()) {
        const size_t end = screen.find('\n', position);
        const std::string_view line =
            screen.substr(position, (end == std::string::npos ? screen.size() : end) - position);
        if (line.find(marker) != std::string::npos) return index;
        if (end == std::string::npos) break;
        position = end + 1;
        ++index;
    }
    return std::nullopt;
}

bool has_exact_line(std::string_view screen, std::string_view expected)
{
    size_t position = 0;
    while (position <= screen.size()) {
        const size_t end = screen.find('\n', position);
        const std::string_view line =
            screen.substr(position, (end == std::string::npos ? screen.size() : end) - position);
        if (line == expected) return true;
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return false;
}

PixelBand row_band(int line, int rows, int height)
{
    const double row_height = static_cast<double>(height) / static_cast<double>(rows);
    const int top = std::clamp(static_cast<int>(line * row_height), 0, height - 1);
    const int bottom = std::clamp(static_cast<int>((line + 1) * row_height), top + 1, height);
    return { top, bottom - 1 };
}

// The renderer's cell height is a device-pixel value (the D2D target is pinned
// to 96 DPI). Using it is exact; height/rows accumulates rounding error across
// the grid and can put a row band half a row off near the bottom.
PixelBand row_band_cells(int line, float cell_height, int height)
{
    if (cell_height <= 0.0f) return row_band(line, 1, height);
    const double row_height = static_cast<double>(cell_height);
    const int top = std::clamp(static_cast<int>(line * row_height), 0, height - 1);
    const int bottom = std::clamp(static_cast<int>((line + 1) * row_height), top + 1, height);
    return { top, bottom - 1 };
}

void check_line_band(const Options& options, HWND window, const std::wstring& capture_name,
                     int line, int rows, int height, float cell_height, long long minimum,
                     std::string_view what)
{
    const PixelBand band = cell_height > 0.0f ? row_band_cells(line, cell_height, height)
                                              : row_band(line, rows, height);
    CaptureStats band_stats;
    if (!run_capture_script(options, window, capture_name, options.artifacts, band_stats, band.top,
                            band.bottom)) {
        check(false, what);
        return;
    }
    std::cout << "band=" << band.top << ".." << band.bottom
              << " bandInkPixels=" << band_stats.band_ink << '\n';
    check(band_stats.band_ink >= minimum, what);
}

void pump_messages()
{
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

void pump_for(int milliseconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        pump_messages();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    pump_messages();
}

bool wait_for_text_and_paint(termole::Session& session, termole::View& view,
                             std::string_view marker, int timeout_ms, std::string& last_screen)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        const auto text = session.screen_text();
        if (text) {
            last_screen = *text;
            if (last_screen.find(marker) != std::string::npos && view.paint_count() > 0) {
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void force_paint(HWND window, HWND view)
{
    ::RedrawWindow(window, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (view) ::UpdateWindow(view);
    pump_for(400);
    ::RedrawWindow(window, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    pump_for(100);
}

constexpr wchar_t kProbeClass[] = L"TermoleVisualProbeWindow";

bool register_probe_class()
{
    static const bool registered = [] {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = ::DefWindowProcW;
        window_class.hInstance = ::GetModuleHandleW(nullptr);
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
        window_class.lpszClassName = kProbeClass;
        return ::RegisterClassExW(&window_class) != 0;
    }();
    return registered;
}

HWND create_probe_window(const std::wstring& title)
{
    const HWND window = ::CreateWindowExW(
        0, kProbeClass, title.c_str(), WS_POPUP | WS_VISIBLE, 40, 40, kWindowWidth,
        kWindowHeight, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (window) {
        ::ShowWindow(window, SW_SHOWNORMAL);
        ::UpdateWindow(window);
    }
    return window;
}

struct Scenario {
    std::wstring title;
    std::wstring artifact_name;
    std::string marker;
    std::string supporting_check;
    int timeout_ms = 15000;
    bool print_screen = false;
    std::string band_marker;
    std::string band_check;
    std::string pointer_marker;
    std::string pointer_check;
};

int run_view_scenario(const Options& options, termole::Session& session, const Scenario& scenario)
{
    if (!register_probe_class()) {
        std::cerr << "FAIL: could not register the probe window class\n";
        (void)session.terminate();
        return 1;
    }

    const HWND window = create_probe_window(scenario.title);
    if (!window) {
        std::cerr << "FAIL: could not create the probe window\n";
        (void)session.terminate();
        return 1;
    }
    auto created_view = termole::View::create(session, window, {});
    if (!created_view) {
        std::cerr << "FAIL: could not create the View: " << created_view.error().message()
                  << '\n';
        ::DestroyWindow(window);
        (void)session.terminate();
        return 1;
    }
    termole::View view = std::move(*created_view);
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(view.hwnd(), 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
    view.focus();

    std::string last_screen;
    const bool drawn = wait_for_text_and_paint(session, view, scenario.marker, scenario.timeout_ms,
                                               last_screen);
    check(drawn, std::string_view(scenario.supporting_check));
    if (!drawn) std::cerr << "last session screen:\n" << last_screen << '\n';

    force_paint(window, view.hwnd());
    CaptureStats stats;
    const bool captured =
        run_capture_script(options, window, scenario.artifact_name, options.artifacts, stats);
    check(captured, "the capture script produced window statistics");
    if (captured) {
        print_stats(stats);
        check_pixels(stats);
        std::cout << "screenshot="
                  << (options.artifacts / (scenario.artifact_name + L".png")).string() << '\n';
    }
    if (!scenario.band_marker.empty()) {
        const auto size = session.size();
        if (!size) {
            check(false, "read the session size for the row band");
        } else if (size->second > 0) {
            const int rows = static_cast<int>(size->second);
            const int height = client.bottom - client.top;
            const float cell_height = view.cell_height();
            const auto candidate = find_line_index(last_screen, scenario.band_marker);
            if (candidate) {
                std::cout << "band-map rows=" << rows << " height=" << height
                          << " markerLine=" << *candidate << " cellHeight=" << cell_height << '\n';
                const std::wstring band_capture = scenario.artifact_name +
                    (scenario.pointer_marker.empty() ? L"-line" : L"-candidate");
                check_line_band(options, window, band_capture, *candidate, rows, height,
                                cell_height, kMinimumBandInkPixels, scenario.band_check);
            } else {
                check(false, "found the marker line in the session screen");
            }
            if (!scenario.pointer_marker.empty()) {
                const auto pointer = find_line_index(last_screen, scenario.pointer_marker);
                if (pointer) {
                    std::cout << "band-map pointerLine=" << *pointer << '\n';
                    check_line_band(options, window, scenario.artifact_name + L"-pointer", *pointer,
                                    rows, height, cell_height, kMinimumPointerInkPixels,
                                    scenario.pointer_check);
                } else {
                    check(false, "found the pointer line in the session screen");
                }
            }
        }
    }
    if (scenario.print_screen) std::cout << "session screen excerpt:\n" << last_screen << '\n';

    view.reset();
    ::DestroyWindow(window);
    (void)session.terminate();
    return failures == 0 ? 0 : 1;
}

int run_alt_screen(const Options& options)
{
    if (!std::filesystem::is_regular_file(options.child)) {
        std::cerr << "FAIL: alt-screen child not found: " << std::filesystem::path(options.child)
                  << '\n';
        return 1;
    }

    termole::Session::Options session_options;
    session_options.program = options.child;
    session_options.columns = 80;
    session_options.rows = 24;
    auto created_session = termole::Session::create(session_options);
    if (!created_session) {
        std::cerr << "FAIL: could not start the alt-screen child: "
                  << created_session.error().message() << '\n';
        return 1;
    }
    termole::Session session = std::move(*created_session);
    Scenario scenario;
    scenario.title = L"Termole visual probe - alt screen";
    scenario.artifact_name = L"alt-screen";
    scenario.marker = "ALT-SCREEN PIXEL PROBE";
    scenario.supporting_check =
        "the session contains the known alt-screen text (supporting signal, not proof)";
    scenario.timeout_ms = 15000;
    scenario.band_marker = "ALT-SCREEN PIXEL PROBE";
    scenario.band_check = "the known alt-screen line has non-background pixels";
    return run_view_scenario(options, session, scenario);
}

int run_fzf(const Options& options)
{
    if (options.fzf.empty() || !std::filesystem::is_regular_file(options.fzf)) {
        std::cout << "SKIP: fzf.exe not found; the fzf visual scenario needs a real selector\n";
        return kSkipExitCode;
    }

    const std::filesystem::path work = options.artifacts / L"fzf-work";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work, error);
    const std::filesystem::path candidates = work / L"candidates.txt";
    const std::filesystem::path script = work / L"run.cmd";
    if (!write_file(candidates, "apple\r\nbanana\r\ncherry\r\n")) {
        std::cerr << "FAIL: could not write " << candidates.string() << '\n';
        return 1;
    }
    const std::string command = "@echo off\r\ntype \"" + candidates.string() + "\" | \"" +
        std::filesystem::path(options.fzf).string() + "\"\r\n";
    if (!write_file(script, command)) {
        std::cerr << "FAIL: could not write " << script.string() << '\n';
        return 1;
    }

    termole::Session::Options session_options;
    session_options.program = L"cmd.exe";
    session_options.arguments = { L"/D", L"/Q", L"/C", script.wstring() };
    session_options.columns = 80;
    session_options.rows = 20;
    auto created_session = termole::Session::create(session_options);
    if (!created_session) {
        std::cerr << "FAIL: could not start cmd.exe: " << created_session.error().message()
                  << '\n';
        return 1;
    }
    termole::Session session = std::move(*created_session);
    Scenario scenario;
    scenario.title = L"Termole visual probe - fzf";
    scenario.artifact_name = L"fzf";
    scenario.marker = "banana";
    scenario.supporting_check =
        "fzf painted its candidates into the session (supporting signal, not proof)";
    scenario.timeout_ms = 20000;
    scenario.print_screen = true;
    scenario.band_marker = "banana";
    scenario.band_check = "the fzf candidate line has non-background pixels";
    scenario.pointer_marker = "\xE2\x96\x8C";  // U+258C, fzf's default pointer glyph
    scenario.pointer_check = "the fzf pointer glyph has non-background pixels";
    return run_view_scenario(options, session, scenario);
}

template <typename Predicate>
bool wait_for_screen(termole::Session& session, Predicate predicate, int timeout_ms,
                     std::string& last_screen)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        const auto text = session.screen_text();
        if (text) {
            last_screen = *text;
            if (predicate(last_screen)) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool wait_for_paint(termole::View& view, uint64_t after, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        if (view.paint_count() > after) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void post_view_key(HWND view, UINT key)
{
    ::PostMessageW(view, WM_KEYDOWN, key, 0);
    ::PostMessageW(view, WM_KEYUP, key, 0);
}

void post_view_char(HWND view, wchar_t character)
{
    ::PostMessageW(view, WM_CHAR, static_cast<WPARAM>(character), 0);
}

// Real fzf interaction through the published View input path (the same
// WM_KEYDOWN/WM_CHAR handling a user gets), not Session::send_key. fzf owns the
// alternate screen; the test drives it with arrow keys, typed filtering,
// Escape and Enter, and captures pixels at every step. screen_text() is used
// only to locate rows and to report supporting signals.
int run_fzf_interaction(const Options& options)
{
    if (options.fzf.empty() || !std::filesystem::is_regular_file(options.fzf)) {
        std::cout << "SKIP: fzf.exe not found; the fzf interaction scenario needs a real selector\n";
        return kSkipExitCode;
    }

    const std::filesystem::path work = options.artifacts / L"fzf-interaction-work";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work, error);
    const std::filesystem::path candidates = work / L"candidates.txt";
    const std::filesystem::path script = work / L"run.cmd";
    if (!write_file(candidates, "apple\r\nbanana\r\ncherry\r\n")) {
        std::cerr << "FAIL: could not write " << candidates.string() << '\n';
        return 1;
    }
    // MAIN-SCREEN-READY is captured before the alternate screen; after Escape
    // and after Enter the same text must be visible again. The two ping waits
    // give the test time to capture the restored main screen between runs.
    const std::string command = "@echo off\r\n"
        "echo MAIN-SCREEN-READY\r\n"
        "ping -n 8 127.0.0.1 >nul\r\n"
        "type \"" + candidates.string() + "\" | \"" +
        std::filesystem::path(options.fzf).string() + "\"\r\n"
        "echo CANCEL-EXIT-CODE=%errorlevel%\r\n"
        "ping -n 12 127.0.0.1 >nul\r\n"
        "type \"" + candidates.string() + "\" | \"" +
        std::filesystem::path(options.fzf).string() + "\"\r\n"
        "echo ENTER-EXIT-CODE=%errorlevel%\r\n"
        "echo MAIN-SCREEN-RESTORED\r\n"
        "ping -n 60 127.0.0.1 >nul\r\n";
    if (!write_file(script, command)) {
        std::cerr << "FAIL: could not write " << script.string() << '\n';
        return 1;
    }

    termole::Session::Options session_options;
    session_options.program = L"cmd.exe";
    session_options.arguments = { L"/D", L"/Q", L"/C", script.wstring() };
    session_options.columns = 80;
    session_options.rows = 20;
    auto created_session = termole::Session::create(session_options);
    if (!created_session) {
        std::cerr << "FAIL: could not start cmd.exe: " << created_session.error().message() << '\n';
        return 1;
    }
    termole::Session session = std::move(*created_session);

    if (!register_probe_class()) {
        std::cerr << "FAIL: could not register the probe window class\n";
        (void)session.terminate();
        return 1;
    }
    const HWND window = create_probe_window(L"Termole visual probe - fzf interaction");
    if (!window) {
        std::cerr << "FAIL: could not create the probe window\n";
        (void)session.terminate();
        return 1;
    }
    auto created_view = termole::View::create(session, window, {});
    if (!created_view) {
        std::cerr << "FAIL: could not create the View: " << created_view.error().message() << '\n';
        ::DestroyWindow(window);
        (void)session.terminate();
        return 1;
    }
    termole::View view = std::move(*created_view);
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(view.hwnd(), 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
    view.focus();

    const int rows = [&] {
        const auto size = session.size();
        return size ? static_cast<int>(size->second) : 0;
    }();
    const int height = client.bottom - client.top;
    const float cell_height = view.cell_height();
    std::cout << "grid rows=" << rows << " height=" << height
              << " cellHeight=" << cell_height << '\n';
    if (rows <= 0) {
        std::cerr << "FAIL: could not read the session rows\n";
        view.reset();
        ::DestroyWindow(window);
        (void)session.terminate();
        return 1;
    }

    auto capture = [&](const std::wstring& name, CaptureStats& stats) {
        return run_capture_script(options, window, name, options.artifacts, stats);
    };
    auto capture_band = [&](const std::wstring& name, int line, CaptureStats& stats) {
        const PixelBand band = row_band_cells(line, cell_height, height);
        std::cout << "band=" << narrow_ascii(name) << " " << band.top << ".." << band.bottom
                  << '\n';
        return run_capture_script(options, window, name, options.artifacts, stats, band.top,
                                  band.bottom);
    };
    auto line_of = [](const std::string& screen, std::string_view marker) {
        return find_line_index(screen, marker);
    };

    std::string screen;
    const bool main_ready = wait_for_screen(
        session,
        [](const std::string& text) { return text.find("MAIN-SCREEN-READY") != std::string::npos; },
        15000, screen);
    check(main_ready, "the main screen shows the pre-fzf marker (supporting signal)");
    if (main_ready) {
        force_paint(window, view.hwnd());
        CaptureStats main_stats;
        const bool captured = capture(L"00-main-before", main_stats);
        check(captured, "captured the pre-fzf main screen");
        if (captured) {
            print_stats(main_stats);
            check_pixels(main_stats);
        }
        if (const auto line = line_of(screen, "MAIN-SCREEN-READY")) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"00-main-before-marker", *line, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the pre-fzf main-screen marker has visible pixels");
        } else {
            check(false, "found the pre-fzf main-screen marker row");
        }
    }

    // ---- first fzf: arrow movement, then Escape cancel ----
    const bool first_ready = wait_for_screen(
        session,
        [](const std::string& text) {
            return text.find("3/3") != std::string::npos &&
                text.find("apple") != std::string::npos &&
                text.find("CANCEL-EXIT-CODE") == std::string::npos;
        },
        20000, screen);
    check(first_ready, "the real fzf draws its full-screen list (supporting signal)");

    int fzf_footer_line = -1;
    if (first_ready) {
        force_paint(window, view.hwnd());
        CaptureStats initial_stats;
        const bool captured = capture(L"01-fzf-initial", initial_stats);
        check(captured, "captured the initial fzf screen");
        if (captured) {
            print_stats(initial_stats);
            check_pixels(initial_stats);
        }

        const auto apple = line_of(screen, "apple");
        const auto banana = line_of(screen, "banana");
        const auto footer = line_of(screen, "3/3");
        fzf_footer_line = footer ? *footer : -1;
        check(apple.has_value() && banana.has_value(),
              "located the apple and banana candidate rows");

        if (apple && banana) {
            CaptureStats apple_stats;
            CaptureStats banana_stats;
            const bool apple_captured = capture_band(L"01-initial-apple", *apple, apple_stats);
            const bool banana_captured = capture_band(L"01-initial-banana", *banana, banana_stats);
            std::cout << "initial apple strong=" << apple_stats.band_strong
                      << " banana strong=" << banana_stats.band_strong << '\n';
            check(apple_captured && banana_captured &&
                      apple_stats.band_strong >=
                          banana_stats.band_strong + kSelectionStrongDelta,
                  "the initial fzf selection block is visible on the apple row");

            // fzf's default layout is bottom-up: the first item (apple) sits on
            // the bottom candidate row. Arrow up moves the cursor to banana;
            // arrow down returns it to apple. Both directions must repaint a
            // filled block on the other row.
            const uint64_t painted_up = view.paint_count();
            post_view_key(view.hwnd(), VK_UP);
            const bool repainted_up = wait_for_paint(view, painted_up, 5000);
            pump_for(300);
            check(repainted_up, "fzf repainted after the arrow-up key");

            const auto up_screen = session.screen_text();
            const auto up_apple = up_screen ? line_of(*up_screen, "apple") : std::nullopt;
            const auto up_banana = up_screen ? line_of(*up_screen, "banana") : std::nullopt;
            if (up_apple && up_banana) {
                CaptureStats up_full;
                const bool up_captured = capture(L"02-fzf-after-up", up_full);
                check(up_captured, "captured fzf after the arrow-up key");
                if (up_captured) {
                    print_stats(up_full);
                    check_pixels(up_full);
                }
                CaptureStats up_apple_stats;
                CaptureStats up_banana_stats;
                const bool up_apple_captured =
                    capture_band(L"02-up-apple", *up_apple, up_apple_stats);
                const bool up_banana_captured =
                    capture_band(L"02-up-banana", *up_banana, up_banana_stats);
                std::cout << "after up apple strong=" << up_apple_stats.band_strong
                          << " banana strong=" << up_banana_stats.band_strong << '\n';
                check(up_apple_captured && up_banana_captured &&
                          up_banana_stats.band_strong >=
                              up_apple_stats.band_strong + kSelectionStrongDelta,
                      "the arrow-up key moved the visible selection block to banana");

                const uint64_t painted_down = view.paint_count();
                post_view_key(view.hwnd(), VK_DOWN);
                const bool repainted_down = wait_for_paint(view, painted_down, 5000);
                pump_for(300);
                check(repainted_down, "fzf repainted after the arrow-down key");

                const auto down_screen = session.screen_text();
                const auto down_apple =
                    down_screen ? line_of(*down_screen, "apple") : std::nullopt;
                const auto down_banana =
                    down_screen ? line_of(*down_screen, "banana") : std::nullopt;
                if (down_apple && down_banana) {
                    CaptureStats down_full;
                    const bool down_captured = capture(L"03-fzf-after-down", down_full);
                    check(down_captured, "captured fzf after the arrow-down key");
                    if (down_captured) {
                        print_stats(down_full);
                        check_pixels(down_full);
                    }
                    CaptureStats down_apple_stats;
                    CaptureStats down_banana_stats;
                    const bool down_apple_captured =
                        capture_band(L"03-down-apple", *down_apple, down_apple_stats);
                    const bool down_banana_captured =
                        capture_band(L"03-down-banana", *down_banana, down_banana_stats);
                    std::cout << "after down apple strong=" << down_apple_stats.band_strong
                              << " banana strong=" << down_banana_stats.band_strong << '\n';
                    check(down_apple_captured && down_banana_captured &&
                              down_apple_stats.band_strong >=
                                  down_banana_stats.band_strong + kSelectionStrongDelta,
                          "the arrow-down key moved the visible selection block back to apple");
                } else {
                    check(false, "located the candidate rows after the arrow-down key");
                }
            } else {
                check(false, "located the candidate rows after the arrow-up key");
            }
        }
    }

    post_view_char(view.hwnd(), 0x1B);
    const bool escaped = wait_for_screen(
        session,
        [](const std::string& text) {
            return text.find("CANCEL-EXIT-CODE=130") != std::string::npos;
        },
        15000, screen);
    check(escaped, "Escape cancelled fzf with exit code 130 (supporting signal)");
    if (escaped) {
        pump_for(200);
        force_paint(window, view.hwnd());
        CaptureStats after_escape;
        const bool captured = capture(L"03-main-after-escape", after_escape);
        check(captured, "captured the main screen after Escape");
        if (captured) {
            print_stats(after_escape);
            check_pixels(after_escape);
        }
        const auto marker = line_of(screen, "MAIN-SCREEN-READY");
        const auto cancel = line_of(screen, "CANCEL-EXIT-CODE=130");
        check(marker.has_value() && cancel.has_value(),
              "the restored main screen contains the pre-fzf marker and the cancel marker");
        if (marker) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"03-after-escape-marker", *marker, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the pre-fzf main screen is visible again after Escape");
        }
        if (cancel) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"03-after-escape-cancel", *cancel, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the Escape cancel result is visible on the restored main screen");
            CaptureStats cursor_stats;
            const bool cursor_captured =
                capture_band(L"03-after-escape-cursor", *cancel + 1, cursor_stats);
            check(cursor_captured && cursor_stats.band_strong > 50,
                  "the terminal cursor is visible on the restored main screen");
        }
        check(screen.find("banana") == std::string::npos,
              "Escape produced no selected line (supporting signal)");
        if (fzf_footer_line >= 0) {
            CaptureStats footer_stats;
            const bool band_captured =
                capture_band(L"03-after-escape-old-footer", fzf_footer_line, footer_stats);
            check(band_captured && footer_stats.band_ink <= kMaximumBlankBandInkPixels,
                  "the fzf footer row is blank after returning to the main screen");
        }
    }

    // ---- second fzf: typed filtering, then Enter selection ----
    const bool second_ready = wait_for_screen(
        session,
        [](const std::string& text) {
            return text.find("3/3") != std::string::npos &&
                text.find("apple") != std::string::npos;
        },
        20000, screen);
    check(second_ready, "the second real fzf draws its full-screen list (supporting signal)");

    if (second_ready) {
        force_paint(window, view.hwnd());
        CaptureStats second_initial;
        const bool captured = capture(L"04-fzf-second-initial", second_initial);
        check(captured, "captured the second fzf screen before filtering");
        if (captured) {
            print_stats(second_initial);
            check_pixels(second_initial);
        }
        const auto apple = line_of(screen, "apple");
        const auto cherry = line_of(screen, "cherry");
        check(apple.has_value() && cherry.has_value(),
              "located the initial second-run candidate rows");

        const uint64_t painted = view.paint_count();
        for (const wchar_t character : std::wstring_view(L"ban")) {
            post_view_char(view.hwnd(), character);
        }
        const bool filtered = wait_for_screen(
            session,
            [](const std::string& text) {
                return text.find("1/3") != std::string::npos &&
                    text.find("banana") != std::string::npos &&
                    text.find("apple") == std::string::npos &&
                    text.find("cherry") == std::string::npos;
            },
            10000, screen);
        check(filtered, "typing ban filtered the list to one candidate (supporting signal)");
        (void)wait_for_paint(view, painted, 5000);
        pump_for(300);
        force_paint(window, view.hwnd());

        CaptureStats filtered_stats;
        const bool filtered_captured = capture(L"05-fzf-filtered", filtered_stats);
        check(filtered_captured, "captured fzf after the typed filter");
        if (filtered_captured) {
            print_stats(filtered_stats);
            check_pixels(filtered_stats);
        }
        const auto banana = line_of(screen, "banana");
        const auto footer = line_of(screen, "1/3");
        if (banana) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"05-filtered-banana", *banana, band_stats);
            check(band_captured && band_stats.band_ink > 0 &&
                      band_stats.band_strong >= kMinimumSelectedStrongPixels,
                  "the filtered banana candidate and its selection block are visible");
            for (int offset = 1; offset <= 2; ++offset) {
                const int blank_line = *banana - offset;
                if (blank_line < 0) continue;
                CaptureStats blank_stats;
                const bool blank_captured = capture_band(
                    L"05-filtered-blank-" + std::to_wstring(offset), blank_line, blank_stats);
                std::cout << "filtered blank offset=" << offset
                          << " ink=" << blank_stats.band_ink << '\n';
                check(blank_captured &&
                          blank_stats.band_ink <= kMaximumBlankBandInkPixels,
                      "a candidate row emptied by the filter has no visible ink (offset " +
                          std::to_string(offset) + ")");
            }
        } else {
            check(false, "located the filtered banana row");
        }
        if (footer) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"05-filtered-footer", *footer, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the filtered fzf footer (1/3, one match of three) has visible pixels");
        }
    }

    post_view_key(view.hwnd(), VK_RETURN);
    post_view_char(view.hwnd(), L'\r');
    const bool selected = wait_for_screen(
        session,
        [](const std::string& text) {
            return text.find("ENTER-EXIT-CODE=0") != std::string::npos &&
                text.find("MAIN-SCREEN-RESTORED") != std::string::npos;
        },
        15000, screen);
    check(selected, "Enter selected the filtered banana and fzf exited with 0 "
                    "(supporting signal)");
    if (selected) {
        pump_for(200);
        force_paint(window, view.hwnd());
        CaptureStats after_enter;
        const bool captured = capture(L"06-main-after-enter", after_enter);
        check(captured, "captured the main screen after Enter");
        if (captured) {
            print_stats(after_enter);
            check_pixels(after_enter);
        }
        const auto restored = line_of(screen, "MAIN-SCREEN-RESTORED");
        const auto enter_marker = line_of(screen, "ENTER-EXIT-CODE=0");
        const auto chosen = line_of(screen, "banana");
        check(restored.has_value() && enter_marker.has_value() && chosen.has_value(),
              "the restored main screen contains the selected line and the exit marker");
        if (restored) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"06-after-enter-restored", *restored, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the main screen is visible again after Enter");
        }
        if (chosen) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"06-after-enter-selected", *chosen, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the selected banana line is visible on the restored main screen");
        }
        if (enter_marker) {
            CaptureStats band_stats;
            const bool band_captured =
                capture_band(L"06-after-enter-exit-code", *enter_marker, band_stats);
            check(band_captured && band_stats.band_ink > 0,
                  "the Enter exit-code line is visible on the restored main screen");
        }
        check(screen.find("3/3") == std::string::npos,
              "the fzf footer is gone after Enter (supporting signal)");
    }

    view.reset();
    ::DestroyWindow(window);
    (void)session.terminate();
    return failures == 0 ? 0 : 1;
}

// The standalone app exposes no grid metrics. Measure the same default View in
// this process (same default font and per-monitor DPI context) so the app's
// pixel rows can be mapped exactly instead of assuming a fixed cell height.
struct CellMetrics {
    float width = 0.0f;
    float height = 0.0f;
};

std::optional<CellMetrics> probe_cell_metrics()
{
    if (!register_probe_class()) return std::nullopt;
    termole::Session::Options options;
    options.program = L"cmd.exe";
    options.arguments = { L"/D", L"/Q", L"/C", L"exit" };
    options.columns = 80;
    options.rows = 24;
    auto session = termole::Session::create(options);
    if (!session) return std::nullopt;
    const HWND window = ::CreateWindowExW(
        0, kProbeClass, L"Termole metric probe", WS_POPUP, -30000, -30000, 800, 600, nullptr,
        nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        (void)session->terminate();
        return std::nullopt;
    }
    auto view = termole::View::create(*session, window, {});
    if (!view) {
        ::DestroyWindow(window);
        (void)session->terminate();
        return std::nullopt;
    }
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(view->hwnd(), 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
    const CellMetrics metrics{ view->cell_width(), view->cell_height() };
    view->reset();
    ::DestroyWindow(window);
    (void)session->terminate();
    if (metrics.width <= 0.0f || metrics.height <= 0.0f) return std::nullopt;
    return metrics;
}

struct WindowSearch {
    std::wstring title;
    HWND window = nullptr;
};

BOOL CALLBACK find_titled_window(HWND window, LPARAM parameter)
{
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    wchar_t text[256]{};
    if (::GetWindowTextW(window, text, static_cast<int>(std::size(text))) == 0) return TRUE;
    if (search->title != text || !::IsWindowVisible(window)) return TRUE;
    search->window = window;
    return FALSE;
}

BOOL CALLBACK find_view_child(HWND window, LPARAM parameter)
{
    wchar_t name[64]{};
    if (::GetClassNameW(window, name, static_cast<int>(std::size(name))) == 0) return TRUE;
    if (std::wstring_view(name) != L"TermoleViewWindow") return TRUE;
    *reinterpret_cast<HWND*>(parameter) = window;
    return FALSE;
}

HWND wait_for_app_window(const std::wstring& title, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    WindowSearch search{ title, nullptr };
    while (std::chrono::steady_clock::now() < deadline) {
        ::EnumWindows(find_titled_window, reinterpret_cast<LPARAM>(&search));
        if (search.window) return search.window;
        ::Sleep(50);
    }
    return nullptr;
}

// T06 standalone entry: the public `termole -e` launcher starts the real app
// window, the same script runs real fzf, and keys are posted to the app's View
// child. The app exposes no screen text, so this scenario maps fzf's bottom-up
// layout from the window grid and asserts the captures only; the PNGs are the
// evidence a human can inspect. No --filter/--no-color and no alt-screen bypass.
int run_fzf_standalone(const Options& options)
{
    if (options.fzf.empty() || !std::filesystem::is_regular_file(options.fzf)) {
        std::cout << "SKIP: fzf.exe not found; the standalone fzf scenario needs a real selector\n";
        return kSkipExitCode;
    }
    if (options.app.empty() || !std::filesystem::is_regular_file(options.app)) {
        std::cerr << "FAIL: termole.exe not found: " << std::filesystem::path(options.app) << '\n';
        return 1;
    }

    const std::filesystem::path work = options.artifacts / L"fzf-standalone-work";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work, error);
    const std::filesystem::path candidates = work / L"candidates.txt";
    const std::filesystem::path script = work / L"run.cmd";
    if (!write_file(candidates, "apple\r\nbanana\r\ncherry\r\n")) {
        std::cerr << "FAIL: could not write " << candidates.string() << '\n';
        return 1;
    }
    const std::string command = "@echo off\r\n"
        "echo MAIN-SCREEN-READY\r\n"
        "ping -n 10 127.0.0.1 >nul\r\n"
        "type \"" + candidates.string() + "\" | \"" +
        std::filesystem::path(options.fzf).string() + "\"\r\n"
        "echo CANCEL-EXIT-CODE=%errorlevel%\r\n"
        "ping -n 14 127.0.0.1 >nul\r\n"
        "type \"" + candidates.string() + "\" | \"" +
        std::filesystem::path(options.fzf).string() + "\"\r\n"
        "echo ENTER-EXIT-CODE=%errorlevel%\r\n"
        "echo MAIN-SCREEN-RESTORED\r\n"
        "ping -n 60 127.0.0.1 >nul\r\n";
    if (!write_file(script, command)) {
        std::cerr << "FAIL: could not write " << script.string() << '\n';
        return 1;
    }

    const auto probed_metrics = probe_cell_metrics();
    const std::wstring title = L"Termole T06 standalone " +
        std::to_wstring(::GetCurrentProcessId());
    const std::wstring app_command = quote(options.app) +
        L" --no-config --borderless --columns 81 --rows 25 --title " + quote(title) +
        L" -e cmd.exe /D /Q /C " + quote(script.wstring());
    std::wstring mutable_command = app_command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(options.app.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, nullptr, &startup, &process)) {
        std::cerr << "FAIL: could not start termole.exe: " << ::GetLastError() << '\n';
        return 1;
    }
    ::CloseHandle(process.hThread);

    const HWND window = wait_for_app_window(title, 15000);
    check(window != nullptr, "the standalone termole window appeared");
    HWND view = nullptr;
    if (window) ::EnumChildWindows(window, find_view_child, reinterpret_cast<LPARAM>(&view));
    check(view != nullptr, "the standalone window contains a Termole View");

    int result = 1;
    if (window && view) {
        RECT client{};
        ::GetClientRect(window, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        const float row_height = probed_metrics ? probed_metrics->height
                                                : static_cast<float>(height) / 25.0f;
        const int grid_rows = std::clamp(static_cast<int>(height / row_height), 1, 1000);
        // fzf's bottom-up layout: prompt on the last row, footer above it, then
        // the candidate rows. Both captured windows use the same default font,
        // so the row positions follow the measured cell height.
        const int apple_row = grid_rows - 3;
        const int banana_row = grid_rows - 4;
        const int cherry_row = grid_rows - 5;
        const int footer_row = grid_rows - 2;
        std::cout << "app window=" << width << "x" << height << " gridRows=" << grid_rows
                  << " rowHeight=" << row_height << " cellHeightProbe="
                  << (probed_metrics ? probed_metrics->height : 0.0f) << '\n';

        auto capture = [&](const std::wstring& name, CaptureStats& stats) {
            return run_capture_script(options, window, name, options.artifacts, stats);
        };
        auto band_stats = [&](const std::wstring& name, int line, CaptureStats& stats) {
            const PixelBand band = row_band_cells(line, row_height, height);
            return run_capture_script(options, window, name, options.artifacts, stats, band.top,
                                      band.bottom);
        };
        auto wait_band = [&](int line, int timeout_ms, auto&& predicate) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
            while (std::chrono::steady_clock::now() < deadline) {
                CaptureStats stats;
                if (band_stats(L"poll", line, stats) && predicate(stats)) return true;
                pump_for(50);
            }
            return false;
        };

        const bool main_ready =
            wait_band(0, 15000, [](const CaptureStats& stats) { return stats.band_ink > 0; });
        check(main_ready, "standalone: the main screen shows the pre-fzf marker");
        CaptureStats main_stats;
        const bool main_captured = capture(L"00-main-before", main_stats);
        check(main_captured, "standalone: captured the pre-fzf main screen");
        if (main_captured) {
            print_stats(main_stats);
            check_pixels(main_stats);
        }

        // fzf's bottom-up layout in a 24-row grid: cherry 19, banana 20,
        // apple 21, footer 22, prompt 23.
        const bool fzf_ready = wait_band(apple_row, 25000, [](const CaptureStats& stats) {
            return stats.band_strong >= kMinimumSelectedStrongPixels;
        });
        check(fzf_ready, "standalone: real fzf draws its candidate list and highlight");

        if (fzf_ready) {
            CaptureStats initial_stats;
            const bool initial_captured = capture(L"01-fzf-initial", initial_stats);
            check(initial_captured, "standalone: captured the initial fzf screen");
            if (initial_captured) {
                print_stats(initial_stats);
                check_pixels(initial_stats);
            }
            CaptureStats apple_stats;
            CaptureStats banana_stats;
            const bool apple_captured = band_stats(L"01-initial-apple", apple_row, apple_stats);
            const bool banana_captured =
                band_stats(L"01-initial-banana", banana_row, banana_stats);
            std::cout << "standalone initial apple strong=" << apple_stats.band_strong
                      << " banana strong=" << banana_stats.band_strong << '\n';
            check(apple_captured && banana_captured &&
                      apple_stats.band_strong >= banana_stats.band_strong + kSelectionStrongDelta,
                  "standalone: the initial fzf selection block is visible on apple");

            post_view_key(view, VK_UP);
            const bool moved = wait_band(banana_row, 5000, [](const CaptureStats& stats) {
                return stats.band_strong >= 1000;
            });
            check(moved, "standalone: the arrow-up key moves the fzf selection");
            CaptureStats up_stats;
            const bool up_captured = capture(L"02-fzf-after-up", up_stats);
            check(up_captured, "standalone: captured fzf after the arrow-up key");
            if (up_captured) {
                print_stats(up_stats);
                check_pixels(up_stats);
            }
            CaptureStats up_apple_stats;
            CaptureStats up_banana_stats;
            const bool up_apple_captured = band_stats(L"02-up-apple", apple_row, up_apple_stats);
            const bool up_banana_captured =
                band_stats(L"02-up-banana", banana_row, up_banana_stats);
            std::cout << "standalone after up apple strong=" << up_apple_stats.band_strong
                      << " banana strong=" << up_banana_stats.band_strong << '\n';
            check(up_apple_captured && up_banana_captured &&
                      up_banana_stats.band_strong >=
                          up_apple_stats.band_strong + kSelectionStrongDelta,
                  "standalone: the arrow-up key moved the visible selection block to banana");

            post_view_char(view, 0x1B);
            const bool escaped =
                wait_band(1, 15000, [](const CaptureStats& stats) { return stats.band_ink > 0; });
            check(escaped, "standalone: Escape returns to the main screen");
            if (escaped) {
                CaptureStats after_escape;
                const bool captured = capture(L"03-main-after-escape", after_escape);
                check(captured, "standalone: captured the main screen after Escape");
                if (captured) {
                    print_stats(after_escape);
                    check_pixels(after_escape);
                }
                CaptureStats marker_stats;
                CaptureStats cancel_stats;
                CaptureStats footer_stats;
                const bool marker_captured = band_stats(L"03-after-escape-marker", 0, marker_stats);
                const bool cancel_captured = band_stats(L"03-after-escape-cancel", 1, cancel_stats);
                const bool footer_captured =
                    band_stats(L"03-after-escape-old-footer", footer_row, footer_stats);
                check(marker_captured && marker_stats.band_ink > 0,
                      "standalone: the pre-fzf main screen is visible again after Escape");
                check(cancel_captured && cancel_stats.band_ink > 0,
                      "standalone: the Escape cancel result is visible");
                CaptureStats cursor_stats;
                const bool cursor_captured = band_stats(L"03-after-escape-cursor", 2, cursor_stats);
                check(cursor_captured && cursor_stats.band_strong > 50,
                      "standalone: the terminal cursor is visible on the restored main screen");
                check(footer_captured && footer_stats.band_ink <= kMaximumBlankBandInkPixels,
                      "standalone: the fzf footer row is blank after returning to the main screen");
            }

            const bool second_ready = wait_band(apple_row, 25000, [](const CaptureStats& stats) {
                return stats.band_strong >= kMinimumSelectedStrongPixels;
            });
            check(second_ready, "standalone: the second fzf run draws its list");

            if (second_ready) {
                for (const wchar_t character : std::wstring_view(L"ban")) {
                    post_view_char(view, character);
                }
                const bool filtered = wait_band(banana_row, 10000, [](const CaptureStats& stats) {
                    return stats.band_ink <= kMaximumBlankBandInkPixels;
                });
                check(filtered, "standalone: typing ban removes the other candidate rows");
                CaptureStats filtered_stats;
                const bool filtered_captured = capture(L"05-fzf-filtered", filtered_stats);
                check(filtered_captured, "standalone: captured the filtered fzf screen");
                if (filtered_captured) {
                    print_stats(filtered_stats);
                    check_pixels(filtered_stats);
                }
                CaptureStats filtered_banana_stats;
                CaptureStats blank_above_stats;
                CaptureStats blank_two_above_stats;
                CaptureStats filtered_footer_stats;
                const bool filtered_banana_captured =
                    band_stats(L"05-filtered-banana", apple_row, filtered_banana_stats);
                const bool blank_captured =
                    band_stats(L"05-filtered-blank-1", banana_row, blank_above_stats);
                const bool blank_two_captured =
                    band_stats(L"05-filtered-blank-2", cherry_row, blank_two_above_stats);
                const bool filtered_footer_captured =
                    band_stats(L"05-filtered-footer", footer_row, filtered_footer_stats);
                check(filtered_banana_captured &&
                          filtered_banana_stats.band_strong >= kMinimumSelectedStrongPixels,
                      "standalone: the filtered banana row is visible and highlighted");
                check(blank_captured &&
                          blank_above_stats.band_ink <= kMaximumBlankBandInkPixels,
                      "standalone: the row above the filtered candidate is blank");
                check(blank_two_captured &&
                          blank_two_above_stats.band_ink <= kMaximumBlankBandInkPixels,
                      "standalone: the second row above the filtered candidate is blank");
                check(filtered_footer_captured && filtered_footer_stats.band_ink > 0,
                      "standalone: the filtered footer is visible");

                post_view_key(view, VK_RETURN);
                post_view_char(view, L'\r');
                const bool selected = wait_band(4, 15000, [](const CaptureStats& stats) {
                    return stats.band_ink > 0;
                });
                check(selected, "standalone: Enter returns to the main screen with the output");
                if (selected) {
                    CaptureStats after_enter;
                    const bool captured = capture(L"06-main-after-enter", after_enter);
                    check(captured, "standalone: captured the main screen after Enter");
                    if (captured) {
                        print_stats(after_enter);
                        check_pixels(after_enter);
                    }
                    CaptureStats chosen_stats;
                    CaptureStats exit_stats;
                    CaptureStats restored_stats;
                    const bool chosen_captured =
                        band_stats(L"06-after-enter-selected", 2, chosen_stats);
                    const bool exit_captured =
                        band_stats(L"06-after-enter-exit-code", 3, exit_stats);
                    const bool restored_captured =
                        band_stats(L"06-after-enter-restored", 4, restored_stats);
                    check(chosen_captured && chosen_stats.band_ink > 0,
                          "standalone: the selected banana line is visible after Enter");
                    check(exit_captured && exit_stats.band_ink > 0,
                          "standalone: the Enter exit-code line is visible");
                    check(restored_captured && restored_stats.band_ink > 0,
                          "standalone: the main screen is restored after Enter");
                }
            }
        }
        result = failures == 0 ? 0 : 1;
    }

    if (window) ::PostMessageW(window, WM_CLOSE, 0, 0);
    const DWORD waited = ::WaitForSingleObject(process.hProcess, 15000);
    if (waited != WAIT_OBJECT_0) ::TerminateProcess(process.hProcess, 1);
    ::CloseHandle(process.hProcess);
    return result;
}

// ---------------------------------------------------------------------------
// T12 (#13): real mouse and bracketed-paste acceptance.
//
// The controlled t12_input_child runs under a real ConPTY. Mouse and key input
// goes through the View's window messages (SendInput for real cursor/keyboard
// events, WM_CHAR for typed characters); the test never calls Session::write,
// View::report_mouse or the encoding helpers directly. The child records the
// exact VT bytes it receives, and every interaction also gets a PrintWindow
// capture and a fixed-row pixel-band assertion (#27).
// ---------------------------------------------------------------------------

constexpr long long kMinimumInputBandInkPixels = 40;
constexpr long long kMinimumSelectionStrongDelta = 2000;
constexpr int kInputRowTitle = 0;
constexpr int kInputRowModes = 1;
constexpr int kInputRowAlpha = 2;
constexpr int kInputRowBeta = 3;
constexpr int kInputRowMouse = 5;
constexpr int kInputRowPaste = 6;
constexpr int kInputRowText = 7;
constexpr int kInputRowStatus = 8;

size_t count_occurrences(std::string_view text, std::string_view needle)
{
    if (needle.empty()) return 0;
    size_t count = 0;
    size_t position = 0;
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

int hex_digit(char character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

std::string hex_encode(std::string_view bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0xf]);
    }
    return out;
}

// Reconstruct the exact bytes the child read from its console input. The child
// logs every ReadFile chunk; concatenating them is order-preserving even when a
// VT sequence is split across reads.
std::string collect_raw_input(const std::string& log)
{
    std::string raw;
    size_t position = 0;
    while (position < log.size()) {
        const size_t end = log.find('\n', position);
        std::string line = log.substr(
            position, (end == std::string::npos ? log.size() : end) - position);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        constexpr std::string_view prefix = "CHUNK|hex=";
        if (line.rfind(prefix, 0) == 0) {
            const std::string_view hex = std::string_view(line).substr(prefix.size());
            for (size_t index = 0; index + 1 < hex.size(); index += 2) {
                const int high = hex_digit(hex[index]);
                const int low = hex_digit(hex[index + 1]);
                if (high < 0 || low < 0) break;
                raw.push_back(static_cast<char>((high << 4) | low));
            }
        }
        if (end == std::string::npos) break;
        position = end + 1;
    }
    return raw;
}

template <typename Predicate>
bool wait_for_log(const std::filesystem::path& path, Predicate predicate, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        if (predicate(read_file(path))) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

template <typename Predicate>
bool wait_for_condition(Predicate predicate, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        if (predicate()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool raw_input_contains(const std::string& log, std::string_view bytes)
{
    return collect_raw_input(log).find(bytes) != std::string::npos;
}

bool open_system_clipboard()
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (::OpenClipboard(nullptr)) return true;
        ::Sleep(5);
    }
    return false;
}

bool set_clipboard(std::wstring_view text)
{
    if (!open_system_clipboard()) return false;
    ::EmptyClipboard();
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) {
        ::CloseClipboard();
        return false;
    }
    auto* data = static_cast<wchar_t*>(::GlobalLock(memory));
    if (!data) {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }
    std::copy(text.begin(), text.end(), data);
    data[text.size()] = L'\0';
    ::GlobalUnlock(memory);
    ::SetClipboardData(CF_UNICODETEXT, memory);
    ::CloseClipboard();
    return true;
}

std::wstring get_clipboard()
{
    if (!open_system_clipboard()) return {};
    std::wstring text;
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const auto* characters = static_cast<const wchar_t*>(::GlobalLock(data));
        if (characters) text.assign(characters);
        ::GlobalUnlock(data);
    }
    ::CloseClipboard();
    return text;
}

bool wait_for_clipboard(std::wstring_view expected, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        if (get_clipboard() == expected) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

struct InputTarget {
    const Options* options = nullptr;
    HWND top = nullptr;
    HWND view = nullptr;
    termole::Session* session = nullptr;
    termole::View* view_object = nullptr;
    std::filesystem::path log;
    float cell_width = 0.0f;
    float cell_height = 0.0f;
    int columns = 0;
    int rows = 0;
    int height = 0;
    bool standalone = false;
};

void update_input_metrics(InputTarget& target)
{
    RECT client{};
    if (!::GetClientRect(target.top, &client)) return;
    const int width = client.right - client.left;
    target.height = client.bottom - client.top;
    if (target.session) {
        if (const auto size = target.session->size()) {
            target.columns = static_cast<int>(size->first);
            target.rows = static_cast<int>(size->second);
        }
    } else if (target.cell_width > 0.0f && target.cell_height > 0.0f) {
        target.columns = std::clamp(
            static_cast<int>(static_cast<float>(width) / target.cell_width), 1, 1000);
        target.rows = std::clamp(
            static_cast<int>(static_cast<float>(target.height) / target.cell_height), 1, 1000);
    }
}

int input_cell_x(const InputTarget& target, int column)
{
    return std::max(0, static_cast<int>((static_cast<float>(column) + 0.5f) * target.cell_width));
}

int input_cell_y(const InputTarget& target, int row)
{
    return std::max(0, static_cast<int>((static_cast<float>(row) + 0.5f) * target.cell_height));
}

int input_expected_column(const InputTarget& target, int x)
{
    return std::clamp(static_cast<int>(static_cast<float>(x) / target.cell_width), 0,
                      std::max(0, target.columns - 1)) + 1;
}

int input_expected_row(const InputTarget& target, int y)
{
    return std::clamp(static_cast<int>(static_cast<float>(y) / target.cell_height), 0,
                      std::max(0, target.rows - 1)) + 1;
}

bool post_mouse_down(const InputTarget& target, int x, int y, bool shift)
{
    const WPARAM buttons = MK_LBUTTON | (shift ? MK_SHIFT : 0);
    const bool posted =
        ::PostMessageW(target.view, WM_LBUTTONDOWN, buttons, MAKELPARAM(x, y)) != FALSE;
    pump_for(30);
    return posted;
}

bool post_mouse_up(const InputTarget& target, int x, int y)
{
    const bool posted = ::PostMessageW(target.view, WM_LBUTTONUP, 0, MAKELPARAM(x, y)) != FALSE;
    pump_for(30);
    return posted;
}

bool input_click(const InputTarget& target, int x, int y)
{
    const bool pressed = post_mouse_down(target, x, y, false);
    const bool released = post_mouse_up(target, x, y);
    pump_for(150);
    return pressed && released;
}

bool input_drag(const InputTarget& target, int start_x, int start_y, int end_x, int end_y,
                bool shift, const std::function<void()>& during_drag = {})
{
    const bool pressed = post_mouse_down(target, start_x, start_y, shift);
    const bool moved =
        ::PostMessageW(target.view, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end_x, end_y)) != FALSE;
    pump_for(120);
    if (during_drag) during_drag();
    const bool released = post_mouse_up(target, end_x, end_y);
    pump_for(150);
    return pressed && moved && released;
}

bool input_move(const InputTarget& target, int x, int y)
{
    const bool moved = ::PostMessageW(target.view, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)) != FALSE;
    pump_for(150);
    return moved;
}

bool input_wheel(const InputTarget& target, int delta, int x, int y)
{
    POINT screen{ x, y };
    if (!::ClientToScreen(target.view, &screen)) return false;
    const bool posted = ::PostMessageW(target.view, WM_MOUSEWHEEL, MAKEWPARAM(0, delta),
                                       MAKELPARAM(screen.x, screen.y)) != FALSE;
    pump_for(200);
    return posted;
}

bool post_input_key(const InputTarget& target, WPARAM key)
{
    const bool pressed = ::PostMessageW(target.view, WM_KEYDOWN, key, 0) != FALSE;
    const bool released = ::PostMessageW(target.view, WM_KEYUP, key, 0) != FALSE;
    pump_for(100);
    return pressed && released;
}

// Shift+Insert is a key chord, not WM_CHAR. Post the real modifier-key message
// sequence to the View; the View tracks Shift and runs the same
// on_key_down(VK_INSERT) -> paste() path a hardware key chord uses.
bool send_shift_insert(const InputTarget& target)
{
    const bool shift_down = ::PostMessageW(target.view, WM_KEYDOWN, VK_SHIFT, 0) != FALSE;
    const bool insert_down = ::PostMessageW(target.view, WM_KEYDOWN, VK_INSERT, 0) != FALSE;
    const bool insert_up = ::PostMessageW(target.view, WM_KEYUP, VK_INSERT, 0) != FALSE;
    const bool shift_up = ::PostMessageW(target.view, WM_KEYUP, VK_SHIFT, 0) != FALSE;
    pump_for(200);
    return shift_down && insert_down && insert_up && shift_up;
}

void post_input_text(const InputTarget& target, std::string_view ascii)
{
    for (const unsigned char character : ascii) {
        ::PostMessageW(target.view, WM_CHAR, static_cast<WPARAM>(character), 0);
    }
    pump_for(30);
}

void post_input_char(const InputTarget& target, wchar_t character)
{
    ::PostMessageW(target.view, WM_CHAR, static_cast<WPARAM>(character), 0);
    pump_for(30);
}

bool capture_input(const InputTarget& target, const std::wstring& name, CaptureStats& stats)
{
    force_paint(target.top, target.view);
    return run_capture_script(*target.options, target.top, name, target.options->artifacts, stats);
}

bool capture_input_band(const InputTarget& target, const std::wstring& name, int line,
                        CaptureStats& stats)
{
    const PixelBand band = row_band_cells(line, target.cell_height, target.height);
    std::cout << "band=" << narrow_ascii(name) << " " << band.top << ".." << band.bottom << '\n';
    return run_capture_script(*target.options, target.top, name, target.options->artifacts,
                              stats, band.top, band.bottom);
}

void check_input_band(const InputTarget& target, const std::wstring& name, int line,
                      long long minimum, std::string_view what)
{
    CaptureStats stats;
    if (!capture_input_band(target, name, line, stats)) {
        check(false, what);
        return;
    }
    std::cout << "bandInkPixels=" << stats.band_ink << " bandStrongPixels=" << stats.band_strong
              << '\n';
    check(stats.band_ink >= minimum, what);
}

bool wait_for_sgr_event(const InputTarget& target, int button, int column, int row, bool press,
                        int timeout_ms = 5000)
{
    const std::string needle = "EVENT|sgr|b=" + std::to_string(button) + "|x=" +
        std::to_string(column) + "|y=" + std::to_string(row) +
        "|press=" + (press ? "1" : "0");
    return wait_for_log(
        target.log, [&](const std::string& log) { return log.find(needle) != std::string::npos; },
        timeout_ms);
}

bool apply_cursor_mode(InputTarget& target, bool application)
{
    const size_t commands = count_occurrences(read_file(target.log), "CMD|CURSOR|");
    post_input_text(target, application ? "~CURSOR=APP~" : "~CURSOR=NORMAL~");
    const bool logged = wait_for_log(
        target.log,
        [&](const std::string& log) {
            return count_occurrences(log, "CMD|CURSOR|") > commands &&
                log.find(std::string("CMD|CURSOR|app=") + (application ? "1" : "0") + "\r\n") !=
                    std::string::npos;
        },
        5000);
    if (!logged) return false;
    if (target.session) {
        std::string screen;
        (void)wait_for_screen(
            *target.session,
            [&](const std::string& text) {
                return has_exact_line(text, application ? "STATUS=CURSOR=APP"
                                                        : "STATUS=CURSOR=NORMAL");
            },
            5000, screen);
    } else {
        pump_for(600);
    }
    return true;
}

bool apply_input_modes(InputTarget& target, std::string_view spec, std::string_view expected)
{
    const std::string before = read_file(target.log);
    const size_t commands = count_occurrences(before, "CMD|MODE|");
    post_input_text(target, "~MODE=" + std::string(spec) + "~");
    const bool logged = wait_for_log(
        target.log,
        [&](const std::string& log) {
            return count_occurrences(log, "CMD|MODE|") > commands &&
                log.find("CMD|MODE|modes=" + std::string(expected) + "\r\n") !=
                    std::string::npos;
        },
        5000);
    if (!logged) return false;

    if (target.session) {
        std::string screen;
        (void)wait_for_screen(
            *target.session,
            [&](const std::string& text) {
                return has_exact_line(text, "MODES=" + std::string(expected));
            },
            5000, screen);
    } else {
        // No screen-text API on the standalone entry. The child writes the
        // mode line after the mode sequences; a short pump lets the View parse
        // both before the next input event.
        pump_for(600);
    }
    return true;
}

int run_input_phases(InputTarget& target)
{
    const bool ready = wait_for_log(
        target.log, [](const std::string& log) { return log.find("READY|") != std::string::npos; },
        15000);
    check(ready, "the controlled program started with VT input enabled");
    if (!ready) return 1;

    const UINT dpi = ::GetDpiForWindow(target.top);
    std::cout << "input-entry=" << (target.standalone ? "standalone" : "host")
              << " dpi=" << dpi << " scale=" << (static_cast<double>(dpi) / 96.0)
              << " cell=" << target.cell_width << "x" << target.cell_height
              << " grid=" << target.columns << "x" << target.rows << '\n';

    CaptureStats initial;
    const bool initial_captured = capture_input(target, L"00-input-initial", initial);
    check(initial_captured, "captured the controlled program's initial screen");
    if (initial_captured) {
        print_stats(initial);
        check_pixels(initial);
    }
    check_input_band(target, L"00-input-title", kInputRowTitle, kMinimumInputBandInkPixels,
                     "the controlled program's title is visible");

    // ---- ?1002 + ?1006 + ?2004: click, drag, wheel ----
    check(apply_input_modes(target, "1002,1006,2004", "1002,1006,2004"),
          "enabled ?1002/?1006/?2004 through the target program");
    check_input_band(target, L"01-modes-on", kInputRowModes, kMinimumInputBandInkPixels,
                     "the applied mouse/paste modes are visible");

    const int click_column = 4;
    const int click_row = 1;
    const int click_x = input_cell_x(target, click_column);
    const int click_y = input_cell_y(target, click_row);
    const int click_expected_column = input_expected_column(target, click_x);
    const int click_expected_row = input_expected_row(target, click_y);
    const std::string click_press = "\x1b[<0;" + std::to_string(click_expected_column) + ";" +
        std::to_string(click_expected_row) + "M";
    const std::string click_release = "\x1b[<0;" + std::to_string(click_expected_column) + ";" +
        std::to_string(click_expected_row) + "m";

    check(input_click(target, click_x, click_y), "sent a real SGR click to the target");
    check(wait_for_sgr_event(target, 0, click_expected_column, click_expected_row, true),
          "the target received the SGR click press at the mapped cell");
    check(wait_for_sgr_event(target, 0, click_expected_column, click_expected_row, false),
          "the target received the SGR click release");
    check(raw_input_contains(read_file(target.log), click_press),
          "the wire bytes are the exact SGR press sequence");
    check(raw_input_contains(read_file(target.log), click_release),
          "the wire bytes are the exact SGR release sequence");
    check_input_band(target, L"02-sgr-click", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the reported click is visible");

    const int drag_start_x = input_cell_x(target, 2);
    const int drag_end_x = input_cell_x(target, 10);
    const int drag_y = input_cell_y(target, 2);
    const int drag_expected_column = input_expected_column(target, drag_end_x);
    const int drag_expected_row = input_expected_row(target, drag_y);
    check(input_drag(
              target, drag_start_x, drag_y, drag_end_x, drag_y, false,
              [&] {
                  CaptureStats drag_stats;
                  const bool drag_captured = capture_input(target, L"03-sgr-drag", drag_stats);
                  check(drag_captured, "captured the SGR drag while the button was held");
                  if (drag_captured) {
                      print_stats(drag_stats);
                      check_pixels(drag_stats);
                  }
                  check_input_band(target, L"03-sgr-drag-active", kInputRowMouse,
                                   kMinimumInputBandInkPixels,
                                   "the active drag report is visible");
              }),
          "sent a real SGR drag across the target");
    check(wait_for_sgr_event(target, 32, drag_expected_column, drag_expected_row, true),
          "the target received drag motion (button+32) in ?1002 mode");

    const int wheel_x = input_cell_x(target, 4);
    const int wheel_y = input_cell_y(target, 1);
    const int wheel_column = input_expected_column(target, wheel_x);
    const int wheel_row = input_expected_row(target, wheel_y);
    check(input_wheel(target, 120, wheel_x, wheel_y), "sent a real wheel-up event");
    check(wait_for_sgr_event(target, 64, wheel_column, wheel_row, true),
          "the target received wheel-up (button 64) at the mapped cell");
    check(input_wheel(target, -120, wheel_x, wheel_y), "sent a real wheel-down event");
    check(wait_for_sgr_event(target, 65, wheel_column, wheel_row, true),
          "the target received wheel-down (button 65) at the mapped cell");
    check_input_band(target, L"04-sgr-wheel", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the reported wheel event is visible");

    // ---- ?1000: press/release only, no drag motion ----
    check(apply_input_modes(target, "1000,1006", "1000,1006"),
          "enabled ?1000/?1006 through the target program");
    const size_t drag_events_before = count_occurrences(read_file(target.log), "EVENT|sgr|b=32|");
    const size_t mode_1000_clicks_before =
        count_occurrences(read_file(target.log), "EVENT|sgr|b=0|");
    check(input_click(target, click_x, click_y), "sent a real click in ?1000 mode");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return count_occurrences(log, "EVENT|sgr|b=0|") > mode_1000_clicks_before &&
                      log.find("EVENT|sgr|b=0|x=" + std::to_string(click_expected_column) +
                                   "|y=" + std::to_string(click_expected_row) +
                                   "|press=1") != std::string::npos;
              },
              5000),
          "the target received the ?1000 click");
    check_input_band(target, L"05-1000-click", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the ?1000 click is visible");
    check(input_drag(target, drag_start_x, drag_y, drag_end_x, drag_y, false),
          "sent a drag in ?1000 mode");
    pump_for(300);
    check(count_occurrences(read_file(target.log), "EVENT|sgr|b=32|") == drag_events_before,
          "?1000 does not report drag motion");

    // ---- ?1003: any-event motion without a button ----
    check(apply_input_modes(target, "1003,1006", "1003,1006"),
          "enabled ?1003/?1006 through the target program");
    const int move_x = input_cell_x(target, 6);
    const int move_y = input_cell_y(target, 3);
    const int move_column = input_expected_column(target, move_x);
    const int move_row = input_expected_row(target, move_y);
    check(input_move(target, move_x, move_y), "moved the real cursor without a button");
    check(wait_for_sgr_event(target, 35, move_column, move_row, true),
          "?1003 reports any-event motion (button 35)");
    check_input_band(target, L"06-1003-motion", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the any-event motion report is visible");

    // ---- ?1: application cursor keys ----
    check(apply_cursor_mode(target, true), "enabled application cursor keys (?1)");
    const size_t app_cursor_before =
        count_occurrences(collect_raw_input(read_file(target.log)), "\x1bOA");
    check(post_input_key(target, VK_UP), "sent an arrow-up key in application cursor mode");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return count_occurrences(collect_raw_input(log), "\x1bOA") > app_cursor_before;
              },
              5000),
          "application cursor up is encoded as ESC O A");
    check_input_band(target, L"06b-cursor-app", kInputRowStatus, kMinimumInputBandInkPixels,
                     "the application cursor mode status is visible");
    check(apply_cursor_mode(target, false), "disabled application cursor keys");
    const size_t normal_cursor_before =
        count_occurrences(collect_raw_input(read_file(target.log)), "\x1b[B");
    check(post_input_key(target, VK_DOWN), "sent an arrow-down key in normal cursor mode");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return count_occurrences(collect_raw_input(log), "\x1b[B") >
                      normal_cursor_before;
              },
              5000),
          "normal cursor down is encoded as ESC [ B");
    check_input_band(target, L"06c-cursor-normal", kInputRowStatus, kMinimumInputBandInkPixels,
                     "the normal cursor mode status is visible");

    // ---- ?1006 off: X10 encoding ----
    check(apply_input_modes(target, "1000,1006=0", "1000"),
          "disabled ?1006 to exercise X10 encoding");
    const size_t x10_before = count_occurrences(read_file(target.log), "EVENT|x10|");
    check(input_click(target, click_x, click_y), "sent a real click in X10 mode");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return log.find("EVENT|x10|b=0|x=" + std::to_string(click_expected_column) +
                                      "|y=" + std::to_string(click_expected_row) +
                                      "|press=1") != std::string::npos;
              },
              5000),
          "the target received the X10 click");
    std::string x10_press = "\x1b[M";
    x10_press.push_back(static_cast<char>(32 + 0));
    x10_press.push_back(static_cast<char>(32 + click_expected_column));
    x10_press.push_back(static_cast<char>(32 + click_expected_row));
    std::string x10_release = "\x1b[M";
    x10_release.push_back(static_cast<char>(32 + 3));
    x10_release.push_back(static_cast<char>(32 + click_expected_column));
    x10_release.push_back(static_cast<char>(32 + click_expected_row));
    check(raw_input_contains(read_file(target.log), x10_press),
          "the wire bytes are the exact X10 press sequence");
    check(count_occurrences(read_file(target.log), "EVENT|x10|") > x10_before,
          "the X10 event was decoded");
    check_input_band(target, L"07-x10-click", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the X10 click is visible");

    check(apply_input_modes(target, "1002,1006", "1002,1006"),
          "re-enabled SGR mouse encoding before the resize");

    // ---- resize: the next click must map to the new grid ----
    const int old_columns = target.columns;
    const int old_rows = target.rows;
    RECT resized_client{};
    if (target.standalone) {
        ::MoveWindow(target.top, 80, 80, 900, 560, TRUE);
        pump_for(700);
        ::GetClientRect(target.top, &resized_client);
        target.height = resized_client.bottom - resized_client.top;
        target.columns = std::clamp(
            static_cast<int>(static_cast<float>(resized_client.right - resized_client.left) /
                             target.cell_width),
            1, 1000);
        target.rows = std::clamp(
            static_cast<int>(static_cast<float>(target.height) / target.cell_height), 1, 1000);
    } else {
        ::MoveWindow(target.top, 40, 40, 900, 560, TRUE);
        pump_for(200);
        ::GetClientRect(target.top, &resized_client);
        ::MoveWindow(target.view, 0, 0, resized_client.right - resized_client.left,
                     resized_client.bottom - resized_client.top, TRUE);
        pump_for(500);
        update_input_metrics(target);
    }
    check(target.columns != old_columns || target.rows != old_rows,
          "the window resize changed the terminal grid");
    std::cout << "resized grid=" << target.columns << "x" << target.rows
              << " window=" << (resized_client.right - resized_client.left) << "x"
              << (resized_client.bottom - resized_client.top) << '\n';
    CaptureStats resized;
    const bool resized_captured = capture_input(target, L"08-after-resize", resized);
    check(resized_captured, "captured the resized window");
    if (resized_captured) {
        print_stats(resized);
        check_pixels(resized);
    }
    const int resized_column = 3;
    const int resized_row = 1;
    const int resized_x = input_cell_x(target, resized_column);
    const int resized_y = input_cell_y(target, resized_row);
    const int resized_expected_column = input_expected_column(target, resized_x);
    const int resized_expected_row = input_expected_row(target, resized_y);
    check(input_click(target, resized_x, resized_y), "sent a real click after the resize");
    check(wait_for_sgr_event(target, 0, resized_expected_column, resized_expected_row, true),
          "the post-resize click maps to the new grid cell");
    check_input_band(target, L"09-resized-click", kInputRowMouse, kMinimumInputBandInkPixels,
                     "the post-resize click report is visible");

    // ---- local selection with Shift while mouse reporting is on ----
    check(apply_input_modes(target, "1002,1006,2004", "1002,1006,2004"),
          "re-enabled ?1002/?1006/?2004 for the selection phase");
    check(set_clipboard(L""), "cleared the clipboard before copy");
    CaptureStats selection_before;
    const bool selection_before_captured =
        capture_input_band(target, L"10-selection-before", kInputRowAlpha, selection_before);
    const int selection_y = input_cell_y(target, kInputRowAlpha);
    check(input_drag(target, input_cell_x(target, 0), selection_y, input_cell_x(target, 17),
                     selection_y, true),
          "Shift+drag selected text locally while mouse reporting was on");
    pump_for(200);
    CaptureStats selection_after;
    const bool selection_after_captured =
        capture_input_band(target, L"11-selection-after", kInputRowAlpha, selection_after);
    std::cout << "selection strong before=" << selection_before.band_strong
              << " after=" << selection_after.band_strong << '\n';
    check(selection_before_captured && selection_after_captured &&
              selection_after.band_strong >=
                  selection_before.band_strong + kMinimumSelectionStrongDelta,
          "the local selection block is visible in the captured pixels");
    CaptureStats selection_full;
    const bool selection_full_captured = capture_input(target, L"11-selection", selection_full);
    check(selection_full_captured, "captured the local selection");
    if (selection_full_captured) check_pixels(selection_full);
    const std::string selection_expected = "SELECT-ALPHA alpha";
    if (target.view_object) {
        const auto selected = target.view_object->selection_text();
        check(selected && *selected == selection_expected,
              "the View reports the selected text through the public API");
    }
    post_input_char(target, 0x03);
    check(wait_for_clipboard(std::wstring(selection_expected.begin(), selection_expected.end()),
                             5000),
          "Ctrl+C copied the local selection to the system clipboard");

    // Paste the copied text back through the bracketed-paste path.
    post_input_char(target, 0x16);
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return log.find("PASTE|bracket=1|hex=" + hex_encode(selection_expected)) !=
                      std::string::npos;
              },
              5000),
          "Ctrl+V pasted the selection with bracketed-paste boundaries");
    check(raw_input_contains(read_file(target.log),
                             "\x1b[200~" + selection_expected + "\x1b[201~"),
          "the wire bytes are the exact bracketed-paste sequence");
    check_input_band(target, L"12-bracketed-paste", kInputRowPaste,
                     kMinimumInputBandInkPixels, "the bracketed paste is visible");

    // ---- Shift+Insert uses the real key chord and the same paste path ----
    const std::wstring shift_insert_text = L"shift-insert-line-one\r\nshift-insert-line-two";
    const std::string shift_insert_utf8 = "shift-insert-line-one\r\nshift-insert-line-two";
    check(set_clipboard(shift_insert_text), "set the clipboard for Shift+Insert");
    check(send_shift_insert(target), "sent a real Shift+Insert key chord");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return log.find("PASTE|bracket=1|hex=" + hex_encode(shift_insert_utf8)) !=
                      std::string::npos;
              },
              5000),
          "Shift+Insert pasted with bracketed-paste boundaries");
    check(raw_input_contains(read_file(target.log),
                             "\x1b[200~" + shift_insert_utf8 + "\x1b[201~"),
          "the Shift+Insert wire bytes are the exact bracketed-paste sequence");
    check_input_band(target, L"13-shift-insert", kInputRowPaste, kMinimumInputBandInkPixels,
                     "the Shift+Insert paste is visible");

    // ---- ?2004 off: plain paste, and typed keys are not pastes ----
    check(apply_input_modes(target, "1002,1006", "1002,1006"),
          "disabled ?2004 for the plain-paste phase");
    const std::string plain_paste = "plain-paste-one\r\nplain-paste-two";
    check(set_clipboard(std::wstring(plain_paste.begin(), plain_paste.end())),
          "set the clipboard for a plain paste");
    const size_t paste_events_before = count_occurrences(read_file(target.log), "PASTE|");
    post_input_char(target, 0x16);
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return collect_raw_input(log).find(plain_paste) != std::string::npos;
              },
              5000),
          "the plain paste reached the target without brackets");
    check(count_occurrences(read_file(target.log), "PASTE|") == paste_events_before,
          "no bracketed-paste event was produced when ?2004 is off");
    check(!raw_input_contains(read_file(target.log), "\x1b[200~" + plain_paste),
          "the plain paste has no bracketed-paste start marker");
    check_input_band(target, L"14-plain-paste", kInputRowText, kMinimumInputBandInkPixels,
                     "the plain paste text is visible");

    const size_t typed_paste_events = count_occurrences(read_file(target.log), "PASTE|");
    post_input_text(target, "KEY-TYPED");
    check(wait_for_log(
              target.log,
              [&](const std::string& log) {
                  return collect_raw_input(log).find("KEY-TYPED") != std::string::npos;
              },
              5000),
          "typed keys reached the target as ordinary text");
    check(count_occurrences(read_file(target.log), "PASTE|") == typed_paste_events,
          "typed keys are not treated as a paste");
    check_input_band(target, L"15-typed-text", kInputRowText, kMinimumInputBandInkPixels,
                     "the typed text is visible");

    // ---- mouse modes off: local selection and local wheel scroll ----
    check(apply_input_modes(target, "OFF", "OFF"),
          "disabled all mouse and paste modes");
    const size_t mouse_events_before = count_occurrences(read_file(target.log), "EVENT|");
    check(set_clipboard(L""), "cleared the clipboard before the local-selection copy");
    CaptureStats local_before;
    const bool local_before_captured =
        capture_input_band(target, L"16-local-selection-before", kInputRowBeta, local_before);
    const int local_y = input_cell_y(target, kInputRowBeta);
    check(input_drag(target, input_cell_x(target, 0), local_y, input_cell_x(target, 14), local_y,
                     false),
          "dragged to select text locally after mouse modes were disabled");
    pump_for(200);
    CaptureStats local_after;
    const bool local_after_captured =
        capture_input_band(target, L"17-local-selection-after", kInputRowBeta, local_after);
    std::cout << "local selection strong before=" << local_before.band_strong
              << " after=" << local_after.band_strong << '\n';
    check(local_before_captured && local_after_captured &&
              local_after.band_strong >= local_before.band_strong + kMinimumSelectionStrongDelta,
          "local selection remains visible after disabling mouse modes");
    check(count_occurrences(read_file(target.log), "EVENT|") == mouse_events_before,
          "no mouse event reaches the target after mouse modes are disabled");
    const std::string local_expected = "SELECT-BETA one";
    if (target.view_object) {
        const auto selected = target.view_object->selection_text();
        check(selected && *selected == local_expected,
              "the mouse-off selection text matches the expected cells");
    }
    post_input_char(target, 0x03);
    check(wait_for_clipboard(std::wstring(local_expected.begin(), local_expected.end()), 5000),
          "Ctrl+C copied the mouse-off local selection");

    // ---- mouse modes off: wheel scrolls the View locally ----
    post_input_text(target, "~HISTORY~");
    check(wait_for_log(
              target.log,
              [](const std::string& log) {
                  return log.find("CMD|HISTORY|rows=") != std::string::npos;
              },
              5000),
          "the target wrote scrollback for the local wheel test");
    if (target.session) {
        std::string screen;
        check(wait_for_screen(
                  *target.session,
                  [](const std::string& text) {
                      return text.find("HISTORY-READY") != std::string::npos;
                  },
                  5000, screen),
              "the history frame reached the View");
        const auto scrollback = target.session->scrollback_text();
        check(scrollback && scrollback->find("SCROLL-OLDER-ROW") != std::string::npos,
              "the scrollback contains the older marker row");
    } else {
        check(wait_for_log(
                  target.log,
                  [](const std::string& log) {
                      return log.find("CMD|HISTORY-READY") != std::string::npos;
                  },
                  5000),
              "the history frame was written");
        pump_for(800);
    }
    CaptureStats scroll_before;
    const bool scroll_before_captured =
        capture_input_band(target, L"18-scroll-before", 0, scroll_before);
    check(scroll_before_captured, "captured the top row before the local wheel scroll");
    std::string viewport_before;
    if (target.view_object) {
        const auto viewport = target.view_object->viewport_text();
        if (viewport) viewport_before = *viewport;
    }
    check(input_wheel(target, 120, input_cell_x(target, 4), input_cell_y(target, 1)),
          "sent a real wheel-up with mouse reporting off");
    if (target.view_object) {
        const bool changed = wait_for_condition(
            [&] {
                const auto viewport = target.view_object->viewport_text();
                return viewport && *viewport != viewport_before;
            },
            5000);
        check(changed, "the View viewport changed after the local wheel-up");
    } else {
        pump_for(800);
    }
    CaptureStats scroll_after;
    const bool scroll_after_captured =
        capture_input_band(target, L"19-scroll-after", 0, scroll_after);
    std::cout << "scroll top band before=" << scroll_before.band_ink
              << " after=" << scroll_after.band_ink << '\n';
    const bool before_blank = scroll_before.band_ink <= 5;
    const bool after_blank = scroll_after.band_ink <= 5;
    check(scroll_before_captured && scroll_after_captured && before_blank != after_blank,
          "the local wheel scroll changed the visible top row");
    CaptureStats scroll_full;
    const bool scroll_full_captured = capture_input(target, L"19-scroll", scroll_full);
    check(scroll_full_captured, "captured the local wheel scroll");
    if (scroll_full_captured) check_pixels(scroll_full);

    check(input_wheel(target, -120, input_cell_x(target, 4), input_cell_y(target, 1)),
          "sent a real wheel-down with mouse reporting off");
    if (target.view_object) {
        (void)wait_for_condition(
            [&] {
                const auto viewport = target.view_object->viewport_text();
                return viewport && *viewport == viewport_before;
            },
            5000);
    } else {
        pump_for(800);
    }
    CaptureStats scroll_back;
    const bool scroll_back_captured =
        capture_input_band(target, L"20-scroll-back", 0, scroll_back);
    check(scroll_back_captured && (scroll_back.band_ink <= 5) == before_blank,
          "the wheel-down returned the View to the bottom");

    check(count_occurrences(read_file(target.log), "EVENT|") == mouse_events_before,
          "the local wheel scroll sent no mouse event to the target");

    return failures == 0 ? 0 : 1;
}

int run_input_interaction(const Options& options)
{
    if (!std::filesystem::is_regular_file(options.input_child)) {
        std::cerr << "FAIL: input probe not found: " << std::filesystem::path(options.input_child)
                  << '\n';
        return 1;
    }
    const std::filesystem::path work = options.artifacts / L"input-interaction-work";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work, error);
    const std::filesystem::path log = work / L"input.log";
    std::filesystem::remove(log, error);

    termole::Session::Options session_options;
    session_options.program = options.input_child;
    session_options.arguments = { log.wstring() };
    session_options.columns = 80;
    session_options.rows = 24;
    auto created_session = termole::Session::create(session_options);
    if (!created_session) {
        std::cerr << "FAIL: could not start the input probe: "
                  << created_session.error().message() << '\n';
        return 1;
    }
    termole::Session session = std::move(*created_session);
    if (!register_probe_class()) {
        std::cerr << "FAIL: could not register the probe window class\n";
        (void)session.terminate();
        return 1;
    }
    const HWND window = create_probe_window(L"Termole visual probe - input interaction");
    if (!window) {
        std::cerr << "FAIL: could not create the probe window\n";
        (void)session.terminate();
        return 1;
    }
    auto created_view = termole::View::create(session, window, {});
    if (!created_view) {
        std::cerr << "FAIL: could not create the View: " << created_view.error().message() << '\n';
        ::DestroyWindow(window);
        (void)session.terminate();
        return 1;
    }
    termole::View view = std::move(*created_view);
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(view.hwnd(), 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
    view.focus();
    pump_for(200);

    InputTarget target;
    target.options = &options;
    target.top = window;
    target.view = view.hwnd();
    target.session = &session;
    target.view_object = &view;
    target.log = log;
    target.cell_width = view.cell_width();
    target.cell_height = view.cell_height();
    update_input_metrics(target);

    const int result = run_input_phases(target);

    view.reset();
    ::DestroyWindow(window);
    (void)session.terminate();
    return result;
}

int run_input_standalone(const Options& options)
{
    if (!std::filesystem::is_regular_file(options.input_child)) {
        std::cerr << "FAIL: input probe not found: " << std::filesystem::path(options.input_child)
                  << '\n';
        return 1;
    }
    if (options.app.empty() || !std::filesystem::is_regular_file(options.app)) {
        std::cerr << "FAIL: termole.exe not found: " << std::filesystem::path(options.app) << '\n';
        return 1;
    }
    const auto metrics = probe_cell_metrics();
    if (!metrics) {
        std::cerr << "FAIL: could not probe the default View cell metrics\n";
        return 1;
    }
    const std::filesystem::path work = options.artifacts / L"input-standalone-work";
    std::error_code error;
    std::filesystem::remove_all(work, error);
    std::filesystem::create_directories(work, error);
    const std::filesystem::path log = work / L"input.log";
    std::filesystem::remove(log, error);

    const std::wstring title = L"Termole T12 input " + std::to_wstring(::GetCurrentProcessId());
    const std::wstring app_command = quote(options.app) +
        L" --no-config --borderless --columns 80 --rows 24 --title " + quote(title) + L" -e " +
        quote(options.input_child) + L" " + quote(log.wstring());
    std::wstring mutable_command = app_command;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(options.app.c_str(), mutable_command.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, nullptr, &startup, &process)) {
        std::cerr << "FAIL: could not start termole.exe: " << ::GetLastError() << '\n';
        return 1;
    }
    ::CloseHandle(process.hThread);

    const HWND window = wait_for_app_window(title, 15000);
    check(window != nullptr, "the standalone termole window appeared");
    HWND view = nullptr;
    if (window) ::EnumChildWindows(window, find_view_child, reinterpret_cast<LPARAM>(&view));
    check(view != nullptr, "the standalone window contains a Termole View");

    int result = 1;
    if (window && view) {
        InputTarget target;
        target.options = &options;
        target.top = window;
        target.view = view;
        target.log = log;
        target.cell_width = metrics->width;
        target.cell_height = metrics->height;
        target.standalone = true;
        update_input_metrics(target);
        result = run_input_phases(target);
    }

    if (window) ::PostMessageW(window, WM_CLOSE, 0, 0);
    const DWORD waited = ::WaitForSingleObject(process.hProcess, 15000);
    if (waited != WAIT_OBJECT_0) ::TerminateProcess(process.hProcess, 1);
    ::CloseHandle(process.hProcess);
    return result;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto options = parse_arguments(argc, argv);
    if (!options) return 2;

    std::error_code error;
    std::filesystem::create_directories(options->artifacts, error);
    if (!std::filesystem::is_directory(options->artifacts, error)) {
        std::cerr << "FAIL: could not create the artifact directory: "
                  << options->artifacts.string() << '\n';
        return 2;
    }

    if (options->mode == L"alt-screen") return run_alt_screen(*options);
    if (options->mode == L"fzf") return run_fzf(*options);
    if (options->mode == L"fzf-interaction") return run_fzf_interaction(*options);
    if (options->mode == L"fzf-standalone") return run_fzf_standalone(*options);
    if (options->mode == L"input-interaction") return run_input_interaction(*options);
    if (options->mode == L"input-standalone") return run_input_standalone(*options);

    std::cerr << "FAIL: unknown mode\n";
    return 2;
}
