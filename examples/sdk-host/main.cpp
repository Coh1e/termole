#include <termole/termole.hpp>

#include <windows.h>

#include <optional>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"TermoleSdkHostWindow";

struct Host {
    std::optional<termole::Session> session;
    std::optional<termole::View> view;
};

void layout(Host& host, HWND window)
{
    if (!host.view) return;
    RECT client{};
    ::GetClientRect(window, &client);
    ::MoveWindow(host.view->hwnd(), 0, 0, client.right - client.left,
                 client.bottom - client.top, TRUE);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    auto* host = reinterpret_cast<Host*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCCREATE: {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    case WM_SIZE:
        if (host) layout(*host, window);
        return 0;
    case WM_CLOSE:
        if (host) {
            host->view.reset();
            if (host->session) {
                (void)host->session->terminate();
                host->session.reset();
            }
        }
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show)
{
    Host host;
    auto session = termole::Session::create({});
    if (!session) return 1;
    host.session.emplace(std::move(*session));

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kClassName;
    if (!::RegisterClassExW(&window_class)) return 1;

    const HWND window = ::CreateWindowExW(0, kClassName, L"Termole SDK host", WS_OVERLAPPEDWINDOW,
                                          CW_USEDEFAULT, CW_USEDEFAULT, 1000, 640, nullptr, nullptr,
                                          instance, &host);
    if (!window) return 1;
    auto view = termole::View::create(*host.session, window);
    if (!view) {
        ::DestroyWindow(window);
        return 1;
    }
    host.view.emplace(std::move(*view));
    layout(host, window);
    host.view->focus();
    ::ShowWindow(window, show);
    ::UpdateWindow(window);

    MSG message{};
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
