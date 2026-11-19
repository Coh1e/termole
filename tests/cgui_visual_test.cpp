#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

static int failures = 0;

static void check(bool condition, const char* what)
{
    if (condition) {
        std::printf("PASS: %s\n", what);
    } else {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

static HWND find_window(const wchar_t* title)
{
    for (int attempt = 0; attempt < 100; ++attempt) {
        HWND window = ::FindWindowW(nullptr, title);
        if (window) return window;
        ::Sleep(50);
    }
    return nullptr;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: cgui_visual_test <termole.exe>\n");
        return 2;
    }

    std::wstring command = L"\"";
    command += argv[1];
    command += L"\" --c-gui";
    if (argc >= 3) {
        command += L" --config=";
        command += argv[2];
    } else {
        command += L" --title=C-GUI-Visual --width=700 --height=420";
    }
    command += L" --program=cmd.exe -- /c \"echo C-GUI-VISIBLE & ping -n 8 127.0.0.1 >nul\"";
    std::wstring mutable_command = command;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    check(::CreateProcessW(argv[1], mutable_command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                           nullptr, &startup, &process) != FALSE,
          "C GUI process started");
    if (process.hProcess) {
        HWND window = find_window(L"C-GUI-Visual");
        check(window != nullptr, "C GUI window found");
        if (window) {
            ::Sleep(800);
            RECT rectangle{};
            ::GetWindowRect(window, &rectangle);
            const int width = rectangle.right - rectangle.left;
            const int height = rectangle.bottom - rectangle.top;
            check(width >= 650 && width <= 760, "C GUI configured width");
            check(height >= 370 && height <= 480, "C GUI configured height");

            HDC screen = ::GetDC(nullptr);
            HDC memory = ::CreateCompatibleDC(screen);
            HBITMAP bitmap = ::CreateCompatibleBitmap(screen, width, height);
            HGDIOBJ previous = ::SelectObject(memory, bitmap);
            ::PrintWindow(window, memory, PW_RENDERFULLCONTENT);

            int ink = 0;
            for (int y = 0; y < height; y += 3) {
                for (int x = 0; x < width; x += 3) {
                    const COLORREF pixel = ::GetPixel(memory, x, y);
                    if (pixel != RGB(0, 0, 0) && pixel != RGB(12, 12, 12)) ++ink;
                }
            }
            check(ink > 50, "C GUI window contains rendered text pixels");

            ::SelectObject(memory, previous);
            ::DeleteObject(bitmap);
            ::DeleteDC(memory);
            ::ReleaseDC(nullptr, screen);
            ::PostMessageW(window, WM_CLOSE, 0, 0);
        }
        const DWORD waited = ::WaitForSingleObject(process.hProcess, 10000);
        check(waited == WAIT_OBJECT_0, "C GUI process exits after WM_CLOSE");
        DWORD exit_code = 1;
        ::GetExitCodeProcess(process.hProcess, &exit_code);
        check(exit_code == 0, "C GUI exit code");
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d C GUI visual check(s) failed\n", failures);
        return 1;
    }
    std::printf("PASS: C GUI visual\n");
    return 0;
}
