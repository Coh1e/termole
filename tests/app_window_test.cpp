#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>

namespace {
struct Find {
    DWORD process = 0;
    HWND window = nullptr;
};

BOOL CALLBACK find_window(HWND window, LPARAM parameter)
{
    auto* find = reinterpret_cast<Find*>(parameter);
    DWORD process = 0;
    ::GetWindowThreadProcessId(window, &process);
    if (process == find->process && ::IsWindowVisible(window)) {
        find->window = window;
        return FALSE;
    }
    return TRUE;
}

bool intersects(const RECT& a, const RECT& b)
{
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
    if (!::CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                          &startup, &process)) {
        std::cerr << "could not start termole.exe\n";
        return 1;
    }
    ::CloseHandle(process.hThread);
    Find find{ process.dwProcessId, nullptr };
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!find.window && std::chrono::steady_clock::now() < deadline) {
        ::EnumWindows(find_window, reinterpret_cast<LPARAM>(&find));
        if (!find.window) ::Sleep(50);
    }
    if (!find.window) {
        std::cerr << "termole.exe did not create a visible window\n";
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hProcess);
        return 1;
    }
    RECT rectangle{};
    ::GetWindowRect(find.window, &rectangle);
    const HMONITOR monitor = ::MonitorFromWindow(find.window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{ sizeof(info) };
    ::GetMonitorInfoW(monitor, &info);
    const bool visible_on_screen = intersects(rectangle, info.rcWork);
    std::cout << "rect=" << rectangle.left << "," << rectangle.top << ","
              << rectangle.right << "," << rectangle.bottom << " work=" << info.rcWork.left << ","
              << info.rcWork.top << "," << info.rcWork.right << "," << info.rcWork.bottom << '\n';
    ::PostMessageW(find.window, WM_CLOSE, 0, 0);
    ::WaitForSingleObject(process.hProcess, 5000);
    ::CloseHandle(process.hProcess);
    if (!visible_on_screen) {
        std::cerr << "termole window is outside the monitor work area\n";
        return 1;
    }
    std::cout << "PASS: default window is on-screen\n";
    return 0;
}
