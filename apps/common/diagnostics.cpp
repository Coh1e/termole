#include "common/diagnostics.h"

#include <windows.h>

#include <algorithm>
#include <string>

namespace termole::app {
namespace {

std::string wide_to_utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size,
                          nullptr, nullptr);
    return result;
}

bool write_all(HANDLE handle, const char* data, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        DWORD written = 0;
        if (!::WriteFile(handle, data + offset, static_cast<DWORD>(size - offset), &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

bool write_console(HANDLE handle, std::wstring_view text)
{
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(text.size() - offset, 32767));
        if (!::WriteConsoleW(handle, text.data() + offset, chunk, &written, nullptr) || written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

struct PreparedHandle {
    HANDLE handle = nullptr;
    bool attached = false;
};

PreparedHandle prepare_handle(DWORD standard_handle)
{
    PreparedHandle result;
    HANDLE handle = ::GetStdHandle(standard_handle);
    if (handle && handle != INVALID_HANDLE_VALUE &&
        ::GetFileType(handle) != FILE_TYPE_UNKNOWN) {
        result.handle = handle;
        return result;
    }

    // A GUI-subsystem process launched from a console has invalid standard
    // handles by default; attach to the parent console only when diagnostics
    // are needed, and detach again after writing so the process is not exposed
    // to that console's Ctrl+C/close events.
    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        result.attached = true;
        handle = ::GetStdHandle(standard_handle);
        if (handle && handle != INVALID_HANDLE_VALUE &&
            ::GetFileType(handle) != FILE_TYPE_UNKNOWN) {
            result.handle = handle;
            return result;
        }
        ::FreeConsole();
        result.attached = false;
    } else if (::GetLastError() == ERROR_ACCESS_DENIED) {
        handle = ::GetStdHandle(standard_handle);
        if (handle && handle != INVALID_HANDLE_VALUE &&
            ::GetFileType(handle) != FILE_TYPE_UNKNOWN) {
            result.handle = handle;
        }
    }
    return result;
}

bool write_standard(DWORD standard_handle, std::wstring_view text)
{
    const PreparedHandle prepared = prepare_handle(standard_handle);
    if (!prepared.handle) return false;

    bool ok = false;
    DWORD mode = 0;
    if (::GetConsoleMode(prepared.handle, &mode)) {
        ok = write_console(prepared.handle, text);
    } else {
        const std::string utf8 = wide_to_utf8(text);
        ok = write_all(prepared.handle, utf8.data(), utf8.size());
    }
    if (prepared.attached) ::FreeConsole();
    return ok;
}

} // namespace

std::wstring utf8_to_wide(std::string_view text)
{
    if (text.empty()) return {};
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring win32_error_message(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text;
    if (length > 0 && buffer) {
        text.assign(buffer, length);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n')) text.pop_back();
    }
    if (buffer) ::LocalFree(buffer);
    if (text.empty()) text = L"error " + std::to_wstring(code);
    return text;
}

bool write_standard_output(std::wstring_view text)
{
    return write_standard(STD_OUTPUT_HANDLE, text);
}

bool write_standard_error(std::wstring_view text)
{
    return write_standard(STD_ERROR_HANDLE, text);
}

void show_message(std::wstring_view text, bool error)
{
    ::MessageBoxW(nullptr, std::wstring(text).c_str(), L"Termole",
                  MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

} // namespace termole::app
