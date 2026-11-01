#pragma once

#include <windows.h>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

// Process observation shared by the host and launcher test families. The
// helpers only use public Win32 APIs and the PID file written by the test
// helper, so they do not depend on Termole internals.

namespace termole_test {

inline std::optional<DWORD> read_pid(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    const std::string text{ std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>() };
    if (text.empty()) return std::nullopt;
    DWORD value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }
    return value;
}

inline std::optional<DWORD> wait_for_pid(const std::filesystem::path& path, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::optional<DWORD> previous;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::optional<DWORD> current = read_pid(path);
        // The helper writes the file in one call; require the same value twice
        // so a partially written file cannot be mistaken for the final PID.
        if (current && previous && *current == *previous) return current;
        previous = current;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return std::nullopt;
}

inline bool wait_process_exit(DWORD pid, int timeout_ms)
{
    const HANDLE process =
        ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return true;
    const DWORD wait = ::WaitForSingleObject(process, static_cast<DWORD>(timeout_ms));
    ::CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

inline bool process_alive(DWORD pid)
{
    const HANDLE process =
        ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    const DWORD wait = ::WaitForSingleObject(process, 0);
    ::CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}

inline DWORD handle_count()
{
    DWORD count = 0;
    return ::GetProcessHandleCount(::GetCurrentProcess(), &count) ? count : 0;
}

} // namespace termole_test
