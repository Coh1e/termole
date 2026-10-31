#include "common/console.h"

#include <windows.h>

namespace termole::app {

bool standard_stream_is_redirected(DWORD standard_handle)
{
    const HANDLE handle = ::GetStdHandle(standard_handle);
    if (!handle || handle == INVALID_HANDLE_VALUE) return false;
    const DWORD type = ::GetFileType(handle);
    return type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK;
}

bool console_is_exclusive()
{
    if (!::GetConsoleWindow()) return false;
    DWORD processes[2]{};
    const DWORD count = ::GetConsoleProcessList(processes, 2);
    return count == 1;
}

bool hide_own_console()
{
    if (!console_is_exclusive()) return false;
    const HWND window = ::GetConsoleWindow();
    if (window) ::ShowWindow(window, SW_HIDE);
    return true;
}

void release_console()
{
    ::FreeConsole();
}

} // namespace termole::app
