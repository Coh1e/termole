#include "common/win.h"

#include <algorithm>
#include <cstdio>

namespace termole::win {

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

std::string win32_error_message(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, length);
        ::LocalFree(buffer);
    }

    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        message.pop_back();

    if (message.empty()) {
        return "Windows error " + std::to_string(error);
    }
    return wide_to_utf8(message);
}

Error system_error(std::string_view operation, DWORD error)
{
    return make_error(TERMOLE_STATUS_SYSTEM_ERROR,
                      std::string(operation) + ": " + win32_error_message(error));
}

Error hresult_error(std::string_view operation, HRESULT status)
{
    char code[16]{};
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(status));
    return make_error(TERMOLE_STATUS_SYSTEM_ERROR,
                      std::string(operation) + " failed (" + code + ")");
}

} // namespace termole::win
