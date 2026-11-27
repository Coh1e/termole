#include "common/win.h"
#include "sdk-consumer/capture.h"

#include <objbase.h>
#include <windows.h>

#include <algorithm>
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

bool wait_color(HWND window, bool red)
{
    const char* png = red ? "after-home-end.png" : "initial-frame.png";
    RECT bounds{};
    POINT origin{};
    if (!::GetWindowRect(window, &bounds) || !::ClientToScreen(window, &origin)) {
        std::fprintf(stderr, "%s: could not locate the remote client area\n", png);
        return false;
    }
    // The shared PNG capture includes the title bar. Probe the same three
    // client pixels in the saved image as in the child program's color patch.
    capture::Sample samples[]{ { 80, 80 }, { 160, 80 }, { 240, 80 } };
    for (auto& sample : samples) {
        sample.x += origin.x - bounds.left;
        sample.y += origin.y - bounds.top;
    }
    const ULONGLONG deadline = ::GetTickCount64() + 3000;
    do {
        capture::Stats stats;
        std::string error;
        if (!capture::window(window, png, stats, error, samples)) {
            std::fprintf(stderr, "%s: %s\n", png, error.c_str());
            return false;
        }
        if (std::ranges::all_of(samples, [red](const capture::Sample& sample) {
                const COLORREF pixel = sample.color;
                const int expected = red ? GetRValue(pixel) : GetBValue(pixel);
                const int other = red ? GetBValue(pixel) : GetRValue(pixel);
                return pixel != CLR_INVALID && expected > other + 40 && expected > 90;
            })) return true;
        ::Sleep(30);
    } while (::GetTickCount64() < deadline);
    std::fprintf(stderr, "%s: expected %s, observed COLORREF samples:", png, red ? "red" : "blue");
    for (const auto& sample : samples) {
        std::fprintf(stderr, " (%d,%d)=%#lx", sample.x, sample.y, sample.color);
    }
    std::fputc('\n', stderr);
    return false;
}

bool succeeded(HANDLE process)
{
    DWORD code = 1;
    return ::WaitForSingleObject(process, 4000) == WAIT_OBJECT_0 &&
           ::GetExitCodeProcess(process, &code) && code == 0;
}

int run(int argc, wchar_t** argv)
{
    if (argc != 3) return 1;
    const unique_handle job(::CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                           &limits, sizeof(limits))) return 1;
    const std::wstring name = L"visual-" + std::to_wstring(::GetCurrentProcessId());
    const auto server = launch(job.get(), argv[1],
        L"--server --pipe " + name + L" -- \"" + argv[2] + L"\" --navigation");
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
    if (!window) {
        std::fputs("Could not find the remote client window.\n", stderr);
        return 1;
    }
    if (!wait_color(window, false)) return 1;
    const auto type_navigation = [window](char marker) {
        return ::PostMessageW(window, WM_CHAR, marker, 1) &&
               ::PostMessageW(window, WM_KEYDOWN, VK_HOME, 1) &&
               ::PostMessageW(window, WM_KEYDOWN, VK_END, 1);
    };
    if (!type_navigation('a')) {
        std::fputs("Could not post the application Home/End input to the client.\n", stderr);
        return 1;
    }
    if (!wait_color(window, true)) {
        DWORD exit_code = 0;
        if (::GetExitCodeProcess(client.get(), &exit_code)) {
            std::fprintf(stderr, "Client exit status after Home/End: %lu (259 = still active)\n", exit_code);
        }
        return 1;
    }
    return type_navigation('q') && succeeded(client.get()) &&
        succeeded(server.get()) ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) return 1;
    const int result = run(argc, argv);
    ::CoUninitialize();
    return result;
}
