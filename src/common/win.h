#pragma once

#include "common/error.h"

#include <windows.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace termole::win {

struct handle_closer {
    void operator()(void* handle) const noexcept
    {
        if (handle && handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle);
    }
};

using unique_handle = std::unique_ptr<void, handle_closer>;

struct pseudo_console_closer {
    void operator()(void* pseudo) const noexcept
    {
        if (pseudo) ::ClosePseudoConsole(static_cast<HPCON>(pseudo));
    }
};

using unique_pseudo_console = std::unique_ptr<void, pseudo_console_closer>;

template <class T>
class com_ptr {
public:
    com_ptr() = default;
    ~com_ptr() { reset(); }

    com_ptr(const com_ptr&) = delete;
    com_ptr& operator=(const com_ptr&) = delete;

    com_ptr(com_ptr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}

    com_ptr& operator=(com_ptr&& other) noexcept
    {
        if (this != &other) {
            reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
        }
        return *this;
    }

    [[nodiscard]] T* get() const noexcept { return pointer_; }
    [[nodiscard]] T* operator->() const noexcept { return pointer_; }
    [[nodiscard]] explicit operator bool() const noexcept { return pointer_ != nullptr; }

    T** put() noexcept
    {
        reset();
        return &pointer_;
    }

    void reset(T* pointer = nullptr) noexcept
    {
        if (pointer_) pointer_->Release();
        pointer_ = pointer;
    }

private:
    T* pointer_ = nullptr;
};

std::string wide_to_utf8(std::wstring_view text);
std::string win32_error_message(DWORD error);
Error system_error(std::string_view operation, DWORD error);
Error hresult_error(std::string_view operation, HRESULT status);

} // namespace termole::win
