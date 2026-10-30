#include <termole/termole.hpp>

#include "common/cli.h"
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

struct AppState {
    termole::app::CliOptions cli;
    std::optional<termole::Session> session;
    std::optional<termole::View> view;
    termole::app::SmokeProbe smoke;
    bool closing = false;
    bool reported_error = false;
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
    return options;
}

void layout_view(AppState& state, HWND window)
{
    if (!state.view) return;
    RECT client{};
    if (!::GetClientRect(window, &client)) return;
    ::MoveWindow(state.view->hwnd(), 0, 0, client.right - client.left, client.bottom - client.top, TRUE);
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

        auto view = termole::View::create(*state->session, window);
        if (!view) {
            report_error(*state, termole::app::utf8_to_wide(view.error().message()));
            state->session.reset();
            return -1;
        }
        state->view.emplace(std::move(*view));
        layout_view(*state, window);
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
            } else if (!state->smoke.enabled() && !state->session->running() && !state->closing) {
                ::PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
        return 0;

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

    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AppState state;
    state.cli = std::move(parsed.options);

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
