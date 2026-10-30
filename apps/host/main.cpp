#include <termole/termole.hpp>

#include "common/smoke.h"

#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr wchar_t kWindowClassName[] = L"TermoleHostExampleWindow";
constexpr UINT_PTR kTimerId = 1;

struct HostState {
    std::optional<termole::Session> session;
    std::optional<termole::View> view;
    termole::app::SmokeProbe smoke;
    HWND label = nullptr;
    bool closing = false;
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

void report_error(HWND owner, const std::string& message)
{
    if (automated_run()) return;
    ::MessageBoxA(owner, message.c_str(), "Termole embedding host", MB_OK | MB_ICONERROR);
}

void layout(HostState& state, HWND window)
{
    RECT client{};
    if (!::GetClientRect(window, &client)) return;
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    constexpr int margin = 12;

    if (state.label)
        ::MoveWindow(state.label, margin, 8, width - margin * 2, 20, TRUE);
    if (state.view) {
        ::MoveWindow(state.view->hwnd(), margin, 32, width - margin * 2,
                     height - 32 - margin, TRUE);
    }
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* state = reinterpret_cast<HostState*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    switch (message) {
    case WM_CREATE: {
        state = reinterpret_cast<HostState*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (!state) return -1;

        state->label = ::CreateWindowExW(
            0, L"STATIC", L"Host-owned layout  |  this child view was created through termole.dll",
            WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, window, nullptr,
            ::GetModuleHandleW(nullptr), nullptr);

        auto session = termole::Session::create();
        if (!session) {
            report_error(window, session.error().message());
            return -1;
        }
        state->session.emplace(std::move(*session));

        auto view = termole::View::create(*state->session, window);
        if (!view) {
            report_error(window, view.error().message());
            state->session.reset();
            return -1;
        }
        state->view.emplace(std::move(*view));
        layout(*state, window);
        state->view->focus();
        state->smoke.start(*state->session);
        ::SetTimer(window, kTimerId, 100, nullptr);
        return 0;
    }

    case WM_SIZE:
        if (state) layout(*state, window);
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
        limits->ptMinTrackSize.x = 400;
        limits->ptMinTrackSize.y = 260;
        return 0;
    }

    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HostState state;
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kWindowClassName;

    if (!::RegisterClassExW(&window_class)) return 1;

    const HWND window = ::CreateWindowExW(0, kWindowClassName, L"Termole embedding host",
                                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
                                          nullptr, nullptr, instance, &state);
    if (!window) return 1;

    ::ShowWindow(window, SW_SHOW);
    ::UpdateWindow(window);

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
