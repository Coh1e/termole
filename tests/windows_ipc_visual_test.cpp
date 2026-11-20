#include "common/win.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace {
using termole::win::unique_handle;

unique_handle launch(HANDLE job, const wchar_t* image, const std::wstring& arguments)
{
    std::wstring command = L"\"" + std::wstring(image) + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(image, command.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &child))
        return {};
    unique_handle process(child.hProcess);
    const unique_handle thread(child.hThread);
    if (!::AssignProcessToJobObject(job, process.get()) ||
        ::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        ::TerminateProcess(process.get(), 1);
        return {};
    }
    return process;
}

struct WindowSearch {
    DWORD pid;
    HWND result = nullptr;
};

BOOL CALLBACK find_window(HWND window, LPARAM parameter)
{
    auto& state = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);
    if (pid != state.pid || !::IsWindowVisible(window)) return TRUE;
    state.result = window;
    return FALSE;
}

bool capture_color(HWND window, bool red)
{
    RECT client{};
    if (!::GetClientRect(window, &client) || client.right < 300 || client.bottom < 160) return false;
    HDC screen = ::GetDC(window);
    if (!screen) return false;
    HDC memory = ::CreateCompatibleDC(screen);
    HBITMAP bitmap = ::CreateCompatibleBitmap(screen, client.right, client.bottom);
    bool matched = false;
    if (memory && bitmap) {
        HGDIOBJ previous = ::SelectObject(memory, bitmap);
        if (::PrintWindow(window, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
            matched = true;
            for (int x : { 80, 160, 240 }) {
                const COLORREF pixel = ::GetPixel(memory, x, 80);
                const int expected = red ? GetRValue(pixel) : GetBValue(pixel);
                const int other = red ? GetBValue(pixel) : GetRValue(pixel);
                matched = matched && pixel != CLR_INVALID && expected > other + 40 && expected > 90;
            }
        }
        ::SelectObject(memory, previous);
    }
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    ::ReleaseDC(window, screen);
    return matched;
}

bool wait_color(HWND window, bool red)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (capture_color(window, red)) return true;
        ::Sleep(30);
    }
    return false;
}

bool succeeded(HANDLE process)
{
    DWORD code = 1;
    return ::WaitForSingleObject(process, 4000) == WAIT_OBJECT_0 &&
           ::GetExitCodeProcess(process, &code) && code == 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3) return 1;
    const unique_handle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                           &limits, sizeof(limits))) return 1;
    const std::wstring name = L"visual-" + std::to_wstring(::GetCurrentProcessId());
    const auto server = launch(job.get(), argv[1],
        L"--server --pipe " + name + L" -- \"" + argv[2] + L"\" --latency");
    if (!server) return 1;

    unique_handle client;
    HWND window = nullptr;
    for (int attempt = 0; attempt < 5 && !window; ++attempt) {
        client = launch(job.get(), argv[1], L"--client --pipe " + name);
        if (!client) return 1;
        WindowSearch search{ ::GetProcessId(client.get()) };
        for (int probe = 0; probe < 100 && !search.result; ++probe) {
            if (::WaitForSingleObject(client.get(), 0) != WAIT_TIMEOUT) break;
            ::EnumWindows(find_window, reinterpret_cast<LPARAM>(&search));
            if (!search.result) ::Sleep(30);
        }
        window = search.result;
        if (!window && ::WaitForSingleObject(client.get(), 0) == WAIT_TIMEOUT) return 1;
    }
    if (!window || !wait_color(window, false)) {
        std::fputs("The remote client did not render the initial blue frame.\n", stderr);
        return 1;
    }
    if (!::PostMessageW(window, WM_CHAR, 'a', 1) || !wait_color(window, true)) {
        std::fputs("Remote input did not produce a visible red frame.\n", stderr);
        return 1;
    }
    return ::PostMessageW(window, WM_CHAR, 'q', 1) && succeeded(client.get()) &&
        succeeded(server.get()) ? 0 : 1;
}
