// #27 pixel-level View/window regression harness.
//
// Two scenarios:
//   alt-screen  a controlled child writes known text into the alternate
//               screen; the View is captured with PrintWindow and must show
//               non-background pixels.
//   fzf         real fzf draws its full-screen UI; the View must show the
//               candidate UI. This scenario is red until issue #26 is fixed
//               and is labelled known-red in CTest until then.
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
    std::wstring fzf;
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
        } else if (argument == L"--fzf") {
            if (!take(options.fzf)) return std::nullopt;
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
        std::cerr << "usage: termole_visual_test --mode <alt-screen|fzf> --artifacts <dir> "
                     "--powershell <powershell.exe> --capture <capture-window.ps1> "
                     "[--child <alt_screen_child.exe>] [--fzf <fzf.exe>]\n";
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
    std::filesystem::remove(png, error);
    std::filesystem::remove(stats_path, error);

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

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(options.powershell.c_str(), mutable_command.data(), nullptr, nullptr,
                          FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
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
        return false;
    }

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

PixelBand row_band(int line, int rows, int height)
{
    const double row_height = static_cast<double>(height) / static_cast<double>(rows);
    const int top = std::clamp(static_cast<int>(line * row_height), 0, height - 1);
    const int bottom = std::clamp(static_cast<int>((line + 1) * row_height), top + 1, height);
    return { top, bottom - 1 };
}

void check_line_band(const Options& options, HWND window, const std::wstring& capture_name,
                     int line, int rows, int height, long long minimum,
                     std::string_view what)
{
    const PixelBand band = row_band(line, rows, height);
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
            const auto candidate = find_line_index(last_screen, scenario.band_marker);
            if (candidate) {
                std::cout << "band-map rows=" << rows << " height=" << height
                          << " markerLine=" << *candidate << '\n';
                const std::wstring band_capture = scenario.artifact_name +
                    (scenario.pointer_marker.empty() ? L"-line" : L"-candidate");
                check_line_band(options, window, band_capture, *candidate, rows, height,
                                kMinimumBandInkPixels, scenario.band_check);
            } else {
                check(false, "found the marker line in the session screen");
            }
            if (!scenario.pointer_marker.empty()) {
                const auto pointer = find_line_index(last_screen, scenario.pointer_marker);
                if (pointer) {
                    std::cout << "band-map pointerLine=" << *pointer << '\n';
                    check_line_band(options, window, scenario.artifact_name + L"-pointer", *pointer,
                                    rows, height, kMinimumPointerInkPixels,
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

    std::cerr << "FAIL: unknown mode\n";
    return 2;
}
