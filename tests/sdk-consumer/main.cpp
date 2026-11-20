// External SDK acceptance host for T22.
//
// This executable is built by an independent CMake project from an installed
// Termole development package: it consumes only include/termole/termole.hpp,
// the installed static library. It exercises the public host contract
// through a real ConPTY session and a real child HWND, and records pixel
// evidence for the View.

#include "capture.h"

#include <termole/termole.hpp>

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
#include <vector>

namespace {

int failures = 0;
std::atomic<int> dialog_count{ 0 };

constexpr wchar_t kHostClassName[] = L"TermoleSdkConsumerHost";
constexpr wchar_t kHostTitle[] = L"Termole SDK consumer";

void check(bool condition, std::string_view what)
{
    if (condition) {
        std::cout << "PASS: " << what << '\n';
    } else {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

void pump_messages()
{
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool wait_for(const std::function<bool()>& predicate, int timeout_ms)
{
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        if (predicate()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms)
{
    std::string last;
    const bool found = wait_for(
        [&] {
            auto text = session.screen_text();
            if (text) last = *text;
            return text && text->find(marker) != std::string::npos;
        },
        timeout_ms);
    if (!found && !last.empty()) std::cerr << "screen:\n" << last << '\n';
    return found;
}

bool wait_for_stop(termole::Session& session, int timeout_ms)
{
    return wait_for([&] { return !session.running(); }, timeout_ms);
}

bool wait_for_size_change(termole::Session& session, std::pair<uint16_t, uint16_t> before,
                          int timeout_ms)
{
    return wait_for(
        [&] {
            const auto size = session.size();
            return size && *size != before;
        },
        timeout_ms);
}

std::wstring environment_value(const wchar_t* name)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::string file_contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool write_text_file(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

std::optional<termole::Session> make_session(const std::wstring& program,
                                             std::vector<std::wstring> arguments, uint16_t columns,
                                             uint16_t rows,
                                             const std::filesystem::path& working_directory = {},
                                             std::vector<std::wstring> environment = {})
{
    termole::Session::Options options;
    options.program = program;
    options.arguments = std::move(arguments);
    options.columns = columns;
    options.rows = rows;
    if (!working_directory.empty()) options.working_directory = working_directory.wstring();
    options.environment = std::move(environment);
    auto created = termole::Session::create(options);
    if (!created) {
        std::cerr << "session creation failed: " << created.error().message() << '\n';
        return std::nullopt;
    }
    return std::optional<termole::Session>{ std::move(*created) };
}

std::optional<termole::View> make_view(termole::Session& session, HWND parent,
                                       const termole::View::Options& options = {})
{
    auto created = termole::View::create(session, parent, options);
    if (!created) {
        std::cerr << "view creation failed: " << created.error().message() << '\n';
        return std::nullopt;
    }
    return std::optional<termole::View>{ std::move(*created) };
}

void type_through_view(HWND view, std::wstring_view text)
{
    for (const wchar_t character : text) {
        ::SendMessageW(view, WM_CHAR, static_cast<WPARAM>(character), 0);
    }
}

bool write_clipboard(std::wstring_view text)
{
    bool opened = false;
    for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
        opened = ::OpenClipboard(nullptr) != FALSE;
        if (!opened) ::Sleep(5);
    }
    if (!opened) return false;
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
    const bool stored = ::SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!stored) ::GlobalFree(memory);
    ::CloseClipboard();
    return stored;
}

std::optional<std::wstring> read_clipboard()
{
    bool opened = false;
    for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
        opened = ::OpenClipboard(nullptr) != FALSE;
        if (!opened) ::Sleep(5);
    }
    if (!opened) return std::nullopt;
    std::optional<std::wstring> result;
    const HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (!data) {
        result = std::wstring{};
    } else {
        const auto* characters = static_cast<const wchar_t*>(::GlobalLock(data));
        if (characters) {
            result = std::wstring(characters);
            ::GlobalUnlock(data);
        }
    }
    ::CloseClipboard();
    return result;
}

std::vector<termole::Event> collect_events(termole::Session& session)
{
    std::vector<termole::Event> events;
    for (;;) {
        auto event = session.poll_event();
        if (!event || !event->has_value()) break;
        events.push_back(std::move(**event));
    }
    return events;
}

bool has_event(const std::vector<termole::Event>& events, termole::EventType type)
{
    return std::any_of(events.begin(), events.end(),
                       [type](const termole::Event& event) { return event.type == type; });
}

termole::Event* find_event(std::vector<termole::Event>& events, termole::EventType type)
{
    const auto found = std::find_if(events.begin(), events.end(),
                                    [type](const termole::Event& event) {
                                        return event.type == type;
                                    });
    return found == events.end() ? nullptr : &*found;
}

termole_session_options session_options()
{
    return TERMOLE_SESSION_OPTIONS_INIT;
}

termole_view_options view_options()
{
    return TERMOLE_VIEW_OPTIONS_INIT;
}

termole_event event_options()
{
    return TERMOLE_EVENT_INIT;
}

LRESULT CALLBACK cbt_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HCBT_CREATEWND) {
        const auto* create = reinterpret_cast<const CBT_CREATEWNDW*>(lparam);
        if (create && create->lpcs && create->lpcs->lpszClass &&
            reinterpret_cast<ULONG_PTR>(create->lpcs->lpszClass) > 0xFFFFu &&
            std::wcscmp(create->lpcs->lpszClass, L"#32770") == 0) {
            dialog_count.fetch_add(1);
        }
    }
    return ::CallNextHookEx(nullptr, code, wparam, lparam);
}

struct HostState {
    HWND view = nullptr;
};

void layout(HostState& state, HWND window)
{
    if (!state.view) return;
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(state.view, 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
}

LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<HostState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (state) layout(*state, window);
        return 0;
    case WM_SETFOCUS:
        if (state && state->view) ::SetFocus(state->view);
        return 0;
    case WM_CLOSE:
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

struct WindowEnumeration {
    DWORD process_id = 0;
    int visible = 0;
};

BOOL CALLBACK count_visible_window(HWND window, LPARAM parameter)
{
    auto* state = reinterpret_cast<WindowEnumeration*>(parameter);
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);
    if (process_id == state->process_id && ::IsWindowVisible(window)) ++state->visible;
    return TRUE;
}

struct Arguments {
    std::filesystem::path helper;
    std::filesystem::path artifacts;
};

bool parse_arguments(int argc, wchar_t** argv, Arguments& arguments)
{
    for (int index = 1; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--helper" && index + 1 < argc) {
            arguments.helper = argv[++index];
        } else if (argument == L"--artifacts" && index + 1 < argc) {
            arguments.artifacts = argv[++index];
        } else {
            std::wcerr << L"unknown argument: " << argument << L'\n';
            return false;
        }
    }
    return !arguments.helper.empty() && !arguments.artifacts.empty();
}

bool capture_window(HWND window, const std::filesystem::path& png,
                    const std::filesystem::path& stats_path, capture::Stats& stats,
                    std::string_view label)
{
    std::string error;
    if (!capture::window(window, png, stats, error)) {
        ++failures;
        std::cerr << "FAIL: " << label << " capture: " << error << '\n';
        return false;
    }
    if (!capture::write_stats(stats_path, stats, error)) {
        ++failures;
        std::cerr << "FAIL: " << label << " stats: " << error << '\n';
        return false;
    }
    std::cout << "PASS: " << label << " capture (" << stats.width << 'x' << stats.height
              << ", inkPixels=" << stats.ink_pixels << ", inkRatio=" << stats.ink_ratio
              << ", strongPixels=" << stats.strong_pixels << ")\n";
    return true;
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    Arguments arguments;
    if (!parse_arguments(argc, argv, arguments)) {
        std::cerr << "usage: termole-sdk-consumer --helper <path> --artifacts <dir>\n";
        return 2;
    }

    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com_result = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::error_code filesystem_error;
    std::filesystem::create_directories(arguments.artifacts, filesystem_error);
    if (filesystem_error) {
        std::cerr << "could not create artifacts directory\n";
        return 2;
    }

    HHOOK hook = ::SetWindowsHookExW(WH_CBT, cbt_hook, nullptr, ::GetCurrentThreadId());

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = host_proc;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kHostClassName;
    if (!::RegisterClassExW(&window_class)) {
        std::cerr << "could not register the host window class\n";
        if (hook) ::UnhookWindowsHookEx(hook);
        if (SUCCEEDED(com_result)) ::CoUninitialize();
        return 1;
    }

    HostState state;
    const HWND host = ::CreateWindowExW(0, kHostClassName, kHostTitle, WS_OVERLAPPEDWINDOW, 80, 80,
                                        1010, 690, nullptr, nullptr, window_class.hInstance, &state);
    if (!host) {
        std::cerr << "could not create the host window\n";
        if (hook) ::UnhookWindowsHookEx(hook);
        if (SUCCEEDED(com_result)) ::CoUninitialize();
        return 1;
    }
    ::ShowWindow(host, SW_SHOWNORMAL);
    ::UpdateWindow(host);
    ::SetForegroundWindow(host);
    ::SetActiveWindow(host);
    pump_messages();

    // ABI version and rejection rules are the first public contract the host
    // sees. A smaller options structure or a different major version must fail
    // explicitly instead of being interpreted with the wrong layout.
    check(termole_abi_version() == TERMOLE_ABI_VERSION, "termole_abi_version reports ABI 3");
    check(TERMOLE_ABI_VERSION == 3u, "the frozen v0.1 ABI is version 3");

    termole_session* raw_session = nullptr;
    termole_session_options bad_size = session_options();
    bad_size.size = sizeof(bad_size) - 4;
    check(termole_session_create(&bad_size, &raw_session) == TERMOLE_STATUS_ABI_MISMATCH,
          "a smaller session options structure is rejected");
    check(raw_session == nullptr, "a rejected creation leaves the handle null");
    termole_session_options bad_version = session_options();
    bad_version.abi_version = TERMOLE_ABI_VERSION + 1;
    check(termole_session_create(&bad_version, &raw_session) == TERMOLE_STATUS_ABI_MISMATCH,
          "an incompatible ABI version is rejected");
    check(termole_session_create(nullptr, &raw_session) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "null options are rejected with a structured status");
    check(termole_session_create(&bad_size, nullptr) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "a null output handle is rejected");
    check(std::strlen(termole_last_error()) != 0,
          "the rejected call leaves a thread-local diagnostic");
    termole_free(nullptr);

    termole_view* raw_view = nullptr;
    termole_view_options bad_view = view_options();
    bad_view.size = 4;
    check(termole_view_create(nullptr, host, &bad_view, &raw_view) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "view creation without a session is rejected before ABI checks");
    check(termole_view_hwnd(nullptr) == nullptr, "termole_view_hwnd(NULL) is harmless");

    check(::GetModuleHandleW(L"termole.dll") == nullptr, "the SDK consumer runs without termole.dll");

    // A malformed user configuration must not affect an embedded host: the
    // library does not read the product configuration at all.
    const std::filesystem::path appdata = arguments.artifacts / "config-appdata";
    std::filesystem::create_directories(appdata / "Termole", filesystem_error);
    check(!filesystem_error, "malformed-config fixture directory created");
    check(write_text_file(appdata / "Termole" / "config.toml", "this is [[[ not valid TOML\n"),
          "malformed user config written");
    const std::wstring old_appdata = environment_value(L"APPDATA");
    ::SetEnvironmentVariableW(L"APPDATA", appdata.c_str());

    auto main_session = make_session(L"cmd.exe", { L"/D", L"/Q" }, 80, 24, arguments.artifacts,
                                     { L"PROMPT=SDK>", L"TERMOLE_SDK_PROBE=1" });
    if (old_appdata.empty()) {
        ::SetEnvironmentVariableW(L"APPDATA", nullptr);
    } else {
        ::SetEnvironmentVariableW(L"APPDATA", old_appdata.c_str());
    }
    check(main_session.has_value(), "session creation ignores the malformed user config");
    if (!main_session) {
        ::DestroyWindow(host);
        if (hook) ::UnhookWindowsHookEx(hook);
        if (SUCCEEDED(com_result)) ::CoUninitialize();
        return 1;
    }
    check(main_session->running(), "the real cmd.exe child is running");
    check(wait_for_text(*main_session, "SDK>", 10000), "the real shell prompt reaches the screen");

    const auto raw_handle = main_session->native_handle();
    auto size = main_session->size();
    check(size && size->first == 80 && size->second == 24,
          "the created session reports its 80x24 grid");
    int32_t exit_code = 0;
    check(termole_session_get_exit_code(raw_handle, &exit_code) == TERMOLE_STATUS_INVALID_STATE,
          "exit code is unavailable while the child is running");
    termole_event bad_event = event_options();
    bad_event.size = sizeof(termole_event) - 4;
    check(termole_session_poll_event(raw_handle, &bad_event) == TERMOLE_STATUS_ABI_MISMATCH,
          "a smaller event structure is rejected");
    termole_event empty_event = event_options();
    for (;;) {
        auto drained = main_session->poll_event();
        if (!drained || !drained->has_value()) break;
    }
    check(termole_session_poll_event(raw_handle, &empty_event) == TERMOLE_STATUS_EMPTY,
          "an empty event queue reports EMPTY");
    char* raw_text = nullptr;
    size_t raw_text_size = 0;
    check(termole_session_get_screen_text(raw_handle, &raw_text, &raw_text_size) == TERMOLE_STATUS_OK &&
              raw_text != nullptr && raw_text_size == std::strlen(raw_text),
          "screen text is returned as a NUL-terminated library allocation");
    termole_free(raw_text);

    // View lifetime, host-owned layout and focus.
    termole::View::Options appearance;
    appearance.font_family = L"Consolas";
    appearance.font_size_dip = 20;
    appearance.theme = TERMOLE_THEME_DARK;
    auto main_view = make_view(*main_session, host, appearance);
    check(main_view.has_value(), "the view is created through the public wrapper");
    if (!main_view) {
        ::DestroyWindow(host);
        if (hook) ::UnhookWindowsHookEx(hook);
        if (SUCCEEDED(com_result)) ::CoUninitialize();
        return 1;
    }
    state.view = main_view->hwnd();
    check(state.view != nullptr, "the view exposes its child HWND");
    check(::GetParent(state.view) == host, "the view is a child of the host window");
    check((::GetWindowLongPtrW(state.view, GWL_STYLE) & WS_CHILD) != 0,
          "the view window has the WS_CHILD style");
    check(::GetAncestor(state.view, GA_ROOT) == host, "the view does not create a second top-level window");

    termole_view_options bad_view_options = view_options();
    bad_view_options.abi_version = TERMOLE_ABI_VERSION + 1;
    termole_view* abi_rejected_view = nullptr;
    check(termole_view_create(raw_handle, host, &bad_view_options, &abi_rejected_view) ==
              TERMOLE_STATUS_ABI_MISMATCH &&
              abi_rejected_view == nullptr,
          "an incompatible view options version is rejected");

    auto before_size = main_session->size();
    ::MoveWindow(state.view, 0, 0, 900, 560, TRUE);
    ::UpdateWindow(state.view);
    check(before_size.has_value(), "the session size is queryable before layout");
    if (before_size) {
        check(wait_for_size_change(*main_session, *before_size, 5000),
              "host layout resizes the ConPTY grid");
    }

    ::SetForegroundWindow(host);
    ::SetActiveWindow(host);
    main_view->focus();
    pump_messages();
    bool focused = ::GetFocus() == state.view;
    if (!focused) {
        GUITHREADINFO info{};
        info.cbSize = sizeof(info);
        if (::GetGUIThreadInfo(::GetCurrentThreadId(), &info)) focused = info.hwndFocus == state.view;
    }
    check(focused, "host-directed focus reaches the view");

    // View input reaches the real shell. `set /a` produces a value that never
    // appears in the typed command line, so input echo cannot satisfy it.
    type_through_view(state.view, L"set /a 12345+67890\r");
    check(wait_for_text(*main_session, "80235", 10000),
          "WM_CHAR input through the view executes a real command");

    // A second host-controlled layout change must keep working after input.
    auto second_size = main_session->size();
    ::MoveWindow(state.view, 0, 0, 760, 440, TRUE);
    ::UpdateWindow(state.view);
    if (second_size) {
        check(wait_for_size_change(*main_session, *second_size, 5000),
              "a second host resize updates the session again");
    }
    const auto viewport = main_view->viewport_text();
    check(viewport && viewport->find("80235") != std::string::npos,
          "the visible viewport keeps the command output after a resize");

    const float small_cell_height = main_view->cell_height();
    termole::View::Options larger = appearance;
    larger.font_size_dip = 28;
    larger.theme = TERMOLE_THEME_LIGHT;
    check(main_view->set_appearance(larger).has_value(),
          "appearance changes without recreating the view");
    check(wait_for([&] { return main_view->cell_height() > small_cell_height + 1.0f; }, 2000),
          "the new font size changes the rendered cell metrics");

    termole::View::Options invalid = appearance;
    invalid.font_size_dip = 7;
    const auto invalid_result = main_view->set_appearance(invalid);
    check(!invalid_result && invalid_result.error().status() == TERMOLE_STATUS_INVALID_ARGUMENT,
          "an invalid appearance is rejected with a structured error");

    // Scrollback, selection and host-driven clipboard access.
    auto history = make_session(arguments.helper.wstring(), { L"--lines", L"50" }, 80, 10);
    check(history.has_value(), "controlled history session created");
    if (history) {
        check(wait_for_stop(*history, 10000), "the controlled history child stops");
        auto history_view = make_view(*history, host);
        check(history_view.has_value(), "a view can attach after the child stopped");
        if (history_view) {
            history_view->scroll(1000);
            const auto top = history_view->viewport_text();
            check(top && top->find("line-0000") != std::string::npos,
                  "scrolling exposes the oldest retained row");
            check(history_view->set_selection(0, 0, 0, 8).has_value(), "selection can be set");
            const auto selected = history_view->selection_text();
            check(selected && *selected == "line-0000", "selection text is exact");
            const auto copied = history_view->copy_selection();
            bool clipboard_available = copied.has_value();
            if (!copied) {
                std::cerr << "SKIP: system clipboard is unavailable: " << copied.error().message()
                          << '\n';
            }
            if (clipboard_available) {
                const auto clipboard = read_clipboard();
                check(clipboard && *clipboard == L"line-0000",
                      "copy_selection writes the exact selection to the system clipboard");
            }
            history_view->clear_selection();
            const auto cleared = history_view->selection_text();
            check(cleared && cleared->empty(), "clearing removes the selection");
            const auto scrollback = history->scrollback_text();
            check(scrollback && scrollback->find("line-0000") != std::string::npos,
                  "scrollback text exposes the oldest retained row");
            const auto history_screen = history->screen_text();
            check(history_screen && history_screen->find("line-0049") != std::string::npos,
                  "the current screen keeps the newest rows");
            const auto history_size = history->size();
            check(history_size && history_size->first == 80 && history_size->second == 10,
                  "the controlled session keeps its requested grid");
            check(history->title().has_value(), "the title query works");

            if (clipboard_available) {
                const auto paste_file = arguments.artifacts / "paste-ok.txt";
                std::filesystem::remove(paste_file, filesystem_error);
                const std::wstring paste_command =
                    L"echo SDK-PASTE-OK> \"" + paste_file.wstring() + L"\"";
                if (write_clipboard(paste_command)) {
                    check(main_view->paste().has_value(),
                          "host-driven paste forwards clipboard text");
                    check(main_session->send_key(termole::Key::Enter).has_value(),
                          "the Enter key is encoded by the public session API");
                    check(wait_for([&] { return std::filesystem::exists(paste_file); }, 5000),
                          "the pasted command executes in the real shell");
                    if (std::filesystem::exists(paste_file)) {
                        check(file_contents(paste_file).find("SDK-PASTE-OK") != std::string::npos,
                              "the pasted command produces the expected output");
                    }
                } else {
                    std::cout << "SKIP: clipboard could not be seeded for paste\n";
                }
            }
            check(main_view->paint_count() > 0, "the view reports successful paints");
            check(main_view->cell_width() > 0.0f && main_view->cell_height() > 0.0f,
                  "cell metrics are available through the public query");
        }
        history_view.reset();
        (void)history->terminate();
    }

    // Event queue: title, bell and exit, with the tail output drained first.
    auto events_session = make_session(arguments.helper.wstring(), { L"--events" }, 80, 24);
    check(events_session.has_value(), "controlled event session created");
    if (events_session) {
        check(wait_for_stop(*events_session, 5000), "the controlled event child stops");
        auto* raw_events = events_session->native_handle();
        termole_event bad_events_event = event_options();
        bad_events_event.size = 4;
        check(termole_session_poll_event(raw_events, &bad_events_event) ==
                  TERMOLE_STATUS_ABI_MISMATCH,
              "event ABI checks run before the queue is drained");
        char* raw_title = nullptr;
        size_t raw_title_size = 0;
        check(termole_session_get_title(raw_events, &raw_title, &raw_title_size) == TERMOLE_STATUS_OK &&
                  raw_title != nullptr && std::string(raw_title, raw_title_size) == "Event Title",
              "the title query returns a library allocation");
        termole_free(raw_title);
        auto events = collect_events(*events_session);
        check(has_event(events, termole::EventType::TitleChanged), "title change is queued");
        check(has_event(events, termole::EventType::Bell), "bell is queued");
        auto* exited = find_event(events, termole::EventType::Exited);
        check(exited != nullptr && exited->exit_code == 7, "exit event carries the exit code");
        auto* title = find_event(events, termole::EventType::TitleChanged);
        check(title != nullptr && title->text == "Event Title", "title event carries the title text");
        const auto event_screen = events_session->screen_text();
        check(event_screen && event_screen->find("event-tail") != std::string::npos,
              "tail output is visible before the exit event is consumed");
        const auto reported_exit = events_session->exit_code();
        check(reported_exit && *reported_exit == 7, "the exit code query matches the event");
    }

    // Destroying a view must not stop the session; a new view restores the
    // current screen and the retained history.
    auto detach_session =
        make_session(arguments.helper.wstring(), { L"--delayed-lines" }, 80, 24);
    check(detach_session.has_value(), "controlled detach session created");
    if (detach_session) {
        auto first_view = make_view(*detach_session, host);
        check(first_view.has_value(), "view attached before detach");
        if (first_view) {
            check(wait_for_text(*detach_session, "before-detach", 5000),
                  "output is visible while the view is attached");
            first_view->reset();
        }
        check(wait_for_stop(*detach_session, 5000),
              "the session keeps consuming output without a view");
        const auto detached_screen = detach_session->screen_text();
        check(detached_screen && detached_screen->find("after-detach") != std::string::npos,
              "output produced without a view is retained");
        auto second_view = make_view(*detach_session, host);
        check(second_view.has_value(), "a view can be reattached to the same session");
        if (second_view) {
            const auto reattached = second_view->viewport_text();
            check(reattached && reattached->find("after-detach") != std::string::npos,
                  "the reattached view restores the current screen");
            second_view->reset();
        }
    }

    // Session operations are internally synchronized; queries from a worker
    // thread must not require the UI thread.
    std::atomic<bool> worker_ok{ true };
    std::atomic<bool> worker_done{ false };
    std::thread worker([&] {
        for (int iteration = 0; iteration < 100 && worker_ok.load(); ++iteration) {
            if (!main_session->size()) worker_ok = false;
            if (!main_session->screen_text()) worker_ok = false;
        }
        worker_done = true;
    });
    while (!worker_done.load()) {
        pump_messages();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.join();
    check(worker_ok.load(), "session queries are safe from a worker thread");

    // The library reports errors through status values; it must not open a
    // modal dialog or a second visible top-level window.
    termole::Session::Options missing;
    missing.program = L"Z:\\termole-does-not-exist\\nope.exe";
    const auto failed_session = termole::Session::create(missing);
    check(!failed_session && failed_session.error().status() == TERMOLE_STATUS_NOT_FOUND,
          "a missing program returns a structured NOT_FOUND status");
    check(dialog_count.load() == 0, "the library did not create a modal dialog");
    WindowEnumeration enumeration;
    enumeration.process_id = ::GetCurrentProcessId();
    ::EnumWindows(count_visible_window, reinterpret_cast<LPARAM>(&enumeration));
    check(enumeration.visible == 1,
          "the library created no visible top-level window besides the host");

    // Final visible content and pixel evidence. The marker is produced by the
    // real shell after clearing the screen, so the capture shows actual text.
    ::MoveWindow(state.view, 0, 0, 900, 560, TRUE);
    ::UpdateWindow(state.view);
    (void)main_session->write("cls\r");
    pump_messages();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    (void)main_session->write("echo TERMOLE-SDK-HOST-OK\r");
    check(wait_for_text(*main_session, "TERMOLE-SDK-HOST-OK", 5000),
          "the final visible marker reaches the screen");
    main_view->focus();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pump_messages();

    capture::Stats view_stats;
    capture::Stats host_stats;
    if (capture_window(state.view, arguments.artifacts / "t22-sdk-view.png",
                       arguments.artifacts / "t22-sdk-view.stats.txt", view_stats, "view")) {
        check(view_stats.width > 0 && view_stats.height > 0, "the view capture has a size");
        check(view_stats.ink_pixels >= 500 && view_stats.ink_ratio >= 0.0008,
              "the view capture contains real text pixels");
        check(view_stats.strong_pixels >= 100,
              "the view capture contains strong foreground pixels");
    }
    if (capture_window(host, arguments.artifacts / "t22-sdk-host.png",
                       arguments.artifacts / "t22-sdk-host.stats.txt", host_stats, "host")) {
        check(host_stats.ink_pixels >= 500, "the host capture includes the embedded terminal");
    }

    // Clean shutdown: view first, then the session; the process tree must end
    // promptly and the handle must reject further input.
    if (main_view) main_view->reset();
    check(main_session->terminate().has_value(), "explicit termination reports success");
    check(wait_for_stop(*main_session, 5000), "the terminated session stops promptly");
    const auto final_exit = main_session->exit_code();
    check(final_exit.has_value(), "the exit code becomes available after termination");
    const auto closed_write = main_session->write("x");
    check(!closed_write && closed_write.error().status() == TERMOLE_STATUS_INVALID_STATE,
          "writing after termination returns INVALID_STATE");
    const auto cleanup_start = std::chrono::steady_clock::now();
    main_session.reset();
    const auto cleanup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - cleanup_start)
                                .count();
    check(cleanup_ms < 5000, "session teardown completes promptly");
    check(termole_session_terminate(nullptr) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "terminating a null session returns INVALID_ARGUMENT");

    if (hook) ::UnhookWindowsHookEx(hook);
    ::DestroyWindow(host);
    pump_messages();
    ::UnregisterClassW(kHostClassName, window_class.hInstance);
    if (SUCCEEDED(com_result)) ::CoUninitialize();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T22 external SDK consumer\n";
    return 0;
}
