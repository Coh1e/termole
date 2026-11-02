#include <termole/termole.hpp>

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <iostream>
#include <stdexcept>
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

void pump_for(int milliseconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        pump();
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 20, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}

uint64_t cpu_time()
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!::GetProcessTimes(::GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        throw std::runtime_error("GetProcessTimes failed");
    }
    ULARGE_INTEGER k{}, u{};
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return k.QuadPart + u.QuadPart;
}

void print_memory(const char* phase)
{
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &memory, sizeof(memory))) {
        std::cout << phase << " working_set_bytes=" << memory.WorkingSetSize
                  << " peak_working_set_bytes=" << memory.PeakWorkingSetSize << '\n';
    } else {
        throw std::runtime_error("GetProcessMemoryInfo failed");
    }
}

HWND parent_window()
{
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ::DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"TermoleT21Parent";
    ::RegisterClassW(&window_class);
    const HWND parent = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                          L"TermoleT21Parent", L"t21",
                                          WS_OVERLAPPEDWINDOW, 0, 0, 900, 600, nullptr,
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ::ShowWindow(parent, SW_SHOWNOACTIVATE);
    ::UpdateWindow(parent);
    return parent;
}
} // namespace

int wmain()
{
    print_memory("before_session");
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
    print_memory("session_without_view");
    auto view = termole::View::create(*session, parent);
    if (!view) {
        std::cerr << "view failed: " << view.error().message() << '\n';
        return 1;
    }
    ::MoveWindow(view->hwnd(), 0, 0, 900, 600, TRUE);
    // Focus changes intentionally invalidate a View. Keep this idle sample
    // inactive so foreground restoration from preceding UI tests isn't counted
    // as an unsolicited repaint. Input-to-visible-frame has its own pixel test.
    pump_for(1500);
    print_memory("view_ready");
    const uint64_t idle_start = view->paint_count();
    const auto cpu_start = cpu_time();
    const auto idle_clock = std::chrono::steady_clock::now();
    pump_for(5000);
    const auto idle_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - idle_clock).count();
    std::cout << "idle_wall_ms=" << idle_ms
              << " idle_cpu_ms=" << (cpu_time() - cpu_start) / 10000.0 << '\n';
    print_memory("idle");
    const uint64_t idle_end = view->paint_count();
    if (idle_end != idle_start) {
        std::cerr << "idle view repainted: " << idle_start << " -> " << idle_end << '\n';
        const auto screen = session->screen_text();
        if (screen) std::cerr << "idle screen:\n" << *screen << '\n';
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
    view->reset();
    if (parent) ::DestroyWindow(parent);
    std::cout << "PASS: idle paint count stable, output triggers paint\n";
    return 0;
}
