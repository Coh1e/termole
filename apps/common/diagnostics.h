#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace termole::app {

std::wstring utf8_to_wide(std::string_view text);
std::wstring win32_error_message(DWORD code);

// Writes UTF-8 to a redirected handle, or wide characters to a console.
// Returns false when no usable standard handle exists.
bool write_standard_output(std::wstring_view text);
bool write_standard_error(std::wstring_view text);

void show_message(std::wstring_view text, bool error);

} // namespace termole::app
