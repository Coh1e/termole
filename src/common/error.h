#pragma once

#include <termole/termole.h>

#include <expected>
#include <string>
#include <utility>

namespace termole {

// Internal, typed failure shared by Session, View and Renderer. The C ABI
// returns status and message without reconstructing categories from prose.
struct NativeError {
    termole_status status = TERMOLE_STATUS_SYSTEM_ERROR;
    std::string message;
};

template <class T>
using Result = std::expected<T, NativeError>;

inline NativeError make_error(termole_status status, std::string message)
{
    return NativeError{ status, std::move(message) };
}

} // namespace termole
