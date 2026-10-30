#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {
void pump()
{
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        pump();
        const auto text = session.screen_text();
        if (text && text->find(marker) != std::string::npos) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return false;
}

HWND parent_window()
{
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ::DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"TermoleT21Parent";
    ::RegisterClassW(&window_class);
    const HWND parent = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"TermoleT21Parent", L"t21",
                                          WS_OVERLAPPEDWINDOW, 0, 0, 900, 600, nullptr,
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ::ShowWindow(parent, SW_SHOWNOACTIVATE);
    ::UpdateWindow(parent);
    return parent;
}
} // namespace

int wmain()
{
    const HWND parent = parent_window();
    if (!parent) return 1;
    termole::Session::Options options;
    options.program = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = { L"/D", L"/Q" };
    auto session = termole::Session::create(options);
    if (!session) {
        std::cerr << "session failed: " << session.error().message() << '\n';
        return 1;
    }
    auto view = termole::View::create(*session, parent);
    if (!view) {
        std::cerr << "view failed: " << view.error().message() << '\n';
        return 1;
    }
    ::MoveWindow(view->hwnd(), 0, 0, 900, 600, TRUE);
    view->focus();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    pump();
    const uint64_t idle_start = view->paint_count();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    pump();
    const uint64_t idle_end = view->paint_count();
    if (idle_end != idle_start) {
        std::cerr << "idle view repainted: " << idle_start << " -> " << idle_end << '\n';
        return 1;
    }

    (void)session->write("echo T21-LATENCY");
    (void)session->send_key(termole::Key::Enter);
    if (!wait_for_text(*session, "T21-LATENCY", 5000)) {
        std::cerr << "output did not arrive\n";
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pump();
    const uint64_t after_output = view->paint_count();
    if (after_output <= idle_end) {
        std::cerr << "output did not trigger a paint\n";
        return 1;
    }
    (void)session->write("exit");
    (void)session->send_key(termole::Key::Enter);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    (void)session->terminate();
    if (parent) ::DestroyWindow(parent);
    std::cout << "PASS: idle paint count stable, output triggers paint\n";
    return 0;
}
