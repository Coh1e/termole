#include <termole/termole.hpp>

#include "common/cli.h"
#include "common/config.h"
#include "common/diagnostics.h"
#include "common/smoke.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kWindowClassName[] = L"TermoleStandaloneWindow";
constexpr UINT_PTR kTimerId = 1;
constexpr int kStatusHeight = 26;

struct AppState {
    termole::app::CliOptions cli;
    std::optional<termole::Session> session;
    std::optional<termole::View> view;
    termole::app::SmokeProbe smoke;
    HWND status = nullptr;
    bool closing = false;
    bool reported_error = false;
    bool session_finished = false;
    int exit_code = 0;
};

bool automated_run()
{
#if defined(TERMOLE_ENABLE_SMOKE)
    std::array<wchar_t, 32> buffer{};
    return ::GetEnvironmentVariableW(L"TERMOLE_SMOKE_RESULT", buffer.data(),
                                     static_cast<DWORD>(buffer.size())) != 0;
#else
    return false;
#endif
}

void report_error(AppState& state, const std::wstring& message)
{
    state.reported_error = true;
    const std::wstring text = L"Termole: " + message + L"\n";
    if (!termole::app::write_standard_error(text) && !automated_run()) {
        termole::app::show_message(text, true);
    }
}

termole::Session::Options session_options(const termole::app::CliOptions& cli)
{
    termole::Session::Options options;
    if (cli.program) options.program = *cli.program;
    options.arguments = cli.arguments;
    if (cli.working_directory) options.working_directory = *cli.working_directory;
    options.environment = cli.environment;
    options.columns = cli.columns;
    options.rows = cli.rows;
    return options;
}

void apply_window_options(AppState& state, HWND window, uint16_t columns, uint16_t rows)
{
    LONG style = static_cast<LONG>(::GetWindowLongPtrW(window, GWL_STYLE));
    LONG_PTR extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (state.cli.borderless) {
        style = static_cast<LONG>(WS_POPUP | WS_VISIBLE);
        extended &= ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    }
    if (state.cli.floating) extended |= WS_EX_TOPMOST;
    else extended &= ~WS_EX_TOPMOST;
    if (state.cli.opacity_percent) extended |= WS_EX_LAYERED;

    ::SetWindowLongPtrW(window, GWL_STYLE, style);
    ::SetWindowLongPtrW(window, GWL_EXSTYLE, extended);
    if (state.cli.opacity_percent) {
        const BYTE alpha = static_cast<BYTE>(*state.cli.opacity_percent * 255 / 100);
        ::SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA);
    }
    if (state.cli.window_title) ::SetWindowTextW(window, state.cli.window_title->c_str());
    if (state.cli.window_icon) {
        HICON icon = static_cast<HICON>(
            ::LoadImageW(nullptr, state.cli.window_icon->c_str(), IMAGE_ICON, 0, 0,
                         LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (icon) {
            ::SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            ::SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        } else {
            report_error(state, L"could not load the window icon");
        }
    }

    int width = 1000;
    int height = 640;
    if (state.view && columns > 0 && rows > 0) {
        RECT rectangle{ 0, 0, static_cast<LONG>(state.view->cell_width() * columns),
                        static_cast<LONG>(state.view->cell_height() * rows) };
        ::AdjustWindowRectEx(&rectangle, style, FALSE, static_cast<DWORD>(extended));
        width = rectangle.right - rectangle.left;
        height = rectangle.bottom - rectangle.top;
    } else {
        RECT current{};
        if (::GetWindowRect(window, &current)) {
            width = current.right - current.left;
            height = current.bottom - current.top;
        }
    }

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    if (state.cli.window_x) x = *state.cli.window_x;
    if (state.cli.window_y) y = *state.cli.window_y;
    if (state.cli.center) {
        MONITORINFO monitor{ sizeof(monitor) };
        const HMONITOR handle = ::MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        ::GetMonitorInfoW(handle, &monitor);
        x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
        y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
    }
    ::SetWindowPos(window, state.cli.floating ? HWND_TOPMOST : HWND_TOP, x, y, width, height,
                   SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void layout_view(AppState& state, HWND window)
{
    if (!state.view) return;
    RECT client{};
    if (!::GetClientRect(window, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    int view_height = height;
    if (state.status && ::IsWindowVisible(state.status)) {
        view_height -= kStatusHeight;
        ::MoveWindow(state.status, 0, view_height, width, kStatusHeight, TRUE);
    }
    ::MoveWindow(state.view->hwnd(), 0, 0, width, view_height, TRUE);
}

void show_session_status(AppState& state, HWND window, int exit_code)
{
    state.exit_code = exit_code;
    state.session_finished = true;
    ::KillTimer(window, kTimerId);

    if (state.status) {
        wchar_t text[160]{};
        _snwprintf_s(text, _TRUNCATE,
                     L"Session exited with code %d. Close the window to exit.", exit_code);
        ::SetWindowTextW(state.status, text);
        ::ShowWindow(state.status, SW_SHOW);
    }
    wchar_t title[160]{};
    _snwprintf_s(title, _TRUNCATE, L"Termole - exited with code %d", exit_code);
    ::SetWindowTextW(window, title);
    layout_view(state, window);
    ::InvalidateRect(window, nullptr, TRUE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<AppState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        state = reinterpret_cast<AppState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;

        auto session = termole::Session::create(session_options(state->cli));
        if (!session) {
            report_error(*state, termole::app::utf8_to_wide(session.error().message()));
            return -1;
        }
        state->session.emplace(std::move(*session));

        termole::View::Options view_options;
        if (state->cli.font_family) view_options.font_family = *state->cli.font_family;
        view_options.font_size_dip = state->cli.font_size_dip;
        view_options.theme = state->cli.theme;
        auto view = termole::View::create(*state->session, window, view_options);
        if (!view) {
            report_error(*state, termole::app::utf8_to_wide(view.error().message()));
            state->session.reset();
            return -1;
        }
        state->view.emplace(std::move(*view));
        state->status = ::CreateWindowExW(0, L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, window,
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
        layout_view(*state, window);
        apply_window_options(*state, window, state->cli.columns, state->cli.rows);
        state->view->focus();
        state->smoke.start(*state->session);
        ::SetTimer(window, kTimerId, 100, nullptr);
        return 0;
    }

    case WM_SIZE:
        if (state) layout_view(*state, window);
        return 0;

    case WM_TIMER:
        if (state && wparam == kTimerId && state->session) {
            const auto smoke = state->smoke.poll(*state->session);
            if (smoke == termole::app::SmokeState::Passed) {
                ::DestroyWindow(window);
            } else if (smoke == termole::app::SmokeState::Failed) {
                state->exit_code = 1;
                ::DestroyWindow(window);
            } else if (!state->smoke.enabled() && !state->session->running() &&
                       !state->closing && !state->session_finished) {
                const auto code = state->session->exit_code();
                const int exit_code = code ? *code : 1;
                const bool keep = state->cli.exit_policy == termole::app::ExitPolicy::KeepWindow ||
                    (state->cli.exit_policy == termole::app::ExitPolicy::Auto && exit_code != 0);
                state->exit_code = exit_code;
                if (keep) show_session_status(*state, window, exit_code);
                else ::PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC device = reinterpret_cast<HDC>(wparam);
        ::SetBkColor(device, RGB(12, 12, 12));
        ::SetTextColor(device, RGB(230, 230, 230));
        return reinterpret_cast<LRESULT>(::GetStockObject(BLACK_BRUSH));
    }

    case WM_CLOSE:
        if (state && !state->closing) {
            state->closing = true;
            ::KillTimer(window, kTimerId);
            state->view.reset();
            if (state->session) {
                (void)state->session->terminate();
                state->session.reset();
            }
        }
        ::DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        ::KillTimer(window, kTimerId);
        ::PostQuitMessage(state ? state->exit_code : 0);
        return 0;

    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = 320;
        limits->ptMinTrackSize.y = 200;
        return 0;
    }

    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) {
        const std::wstring text = L"Termole: could not parse the command line\n";
        if (!termole::app::write_standard_error(text) && !automated_run()) {
            termole::app::show_message(text, true);
        }
        return 1;
    }
    auto parsed = termole::app::parse_cli(argc, argv);
    ::LocalFree(argv);

    if (parsed.action == termole::app::CliAction::Help) {
        const std::wstring text = termole::app::help_text();
        if (!termole::app::write_standard_output(text) && !automated_run()) {
            termole::app::show_message(text, false);
        }
        return 0;
    }
    if (parsed.action == termole::app::CliAction::Version) {
        const std::wstring text = termole::app::version_text();
        if (!termole::app::write_standard_output(text) && !automated_run()) {
            termole::app::show_message(text, false);
        }
        return 0;
    }
    if (parsed.action == termole::app::CliAction::Error) {
        const std::wstring text = L"termole: " + parsed.message + L"\n";
        if (!termole::app::write_standard_error(text)) {
            termole::app::show_message(text + L"\nUse 'termole --help' for usage.\n", true);
        }
        return 2;
    }

    AppState state;
    state.cli = std::move(parsed.options);

    if (!state.cli.no_config) {
        const std::wstring path = state.cli.config_path ? *state.cli.config_path
                                                        : termole::app::default_config_path();
        const bool explicit_path = state.cli.config_path.has_value();
        const bool exists = !path.empty() &&
            ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (!path.empty() && (exists || explicit_path)) {
            auto loaded = termole::app::load_config(path, state.cli.profile);
            if (!loaded.ok) {
                report_error(state, L"configuration error: " + loaded.error +
                                        L"\nUsing defaults.");
            } else {
                termole::app::apply_config(state.cli, loaded.config, state.cli.profile);
            }
        }
    }

    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kWindowClassName;

    if (!::RegisterClassExW(&window_class)) {
        report_error(state, L"could not register the window class");
        return 1;
    }

    const HWND window = ::CreateWindowExW(0, kWindowClassName, L"Termole", WS_OVERLAPPEDWINDOW,
                                          CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640, nullptr, nullptr,
                                          instance, &state);
    if (!window) {
        if (!state.reported_error) report_error(state, L"could not create the terminal window");
        return 1;
    }

    ::ShowWindow(window, show_command);
    if (show_command != SW_HIDE) ::UpdateWindow(window);

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
