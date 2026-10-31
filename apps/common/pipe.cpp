#include "common/pipe.h"

#include <windows.h>

#include <algorithm>
#include <iterator>
#include <thread>
#include <utility>
#include <vector>

namespace termole::app {
namespace {

std::wstring wide_error(DWORD code)
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

std::wstring default_shell()
{
    wchar_t buffer[32768]{};
    const DWORD length =
        ::GetEnvironmentVariableW(L"COMSPEC", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer)) return std::wstring(buffer, length);
    return L"cmd.exe";
}

std::wstring quoted(const std::wstring& text)
{
    if (text.empty()) return L"\"\"";
    return L"\"" + text + L"\"";
}

} // namespace

PipeRunner::PipeRunner() : shell_(default_shell()) {}

PipeRunner::PipeRunner(std::wstring shell)
    : shell_(shell.empty() ? default_shell() : std::move(shell))
{
}

bool PipeRunner::launch(const std::wstring& command, std::string text, std::wstring& error)
{
    if (command.empty()) {
        error = L"pipe command is empty";
        return false;
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = static_cast<DWORD>(sizeof(attributes));
    attributes.bInheritHandle = TRUE;

    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!::CreatePipe(&read, &write, &attributes, 0)) {
        error = L"could not create the pipe: " + wide_error(::GetLastError());
        return false;
    }
    if (!::SetHandleInformation(write, HANDLE_FLAG_INHERIT, 0)) {
        error = L"could not make the pipe write end private: " + wide_error(::GetLastError());
        ::CloseHandle(write);
        ::CloseHandle(read);
        return false;
    }

    HANDLE null_output = ::CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &attributes, OPEN_EXISTING, 0, nullptr);
    if (null_output == INVALID_HANDLE_VALUE) {
        error = L"could not open NUL: " + wide_error(::GetLastError());
        ::CloseHandle(write);
        ::CloseHandle(read);
        return false;
    }

    std::wstring command_line = quoted(shell_) + L" /d /s /c \"" + command + L"\"";

    // Inherit only the pipe read end and the NUL handle. Without an explicit
    // list, CreateProcess would copy every inheritable Termole handle into the
    // command and could keep ConPTY/caller pipes open after this window closes.
    SIZE_T attribute_size = 0;
    (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    std::vector<BYTE> attribute_buffer(attribute_size);
    auto* attribute_list =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
    if (!::InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_size)) {
        error = L"could not prepare the pipe handle list: " + wide_error(::GetLastError());
        ::CloseHandle(write);
        ::CloseHandle(read);
        ::CloseHandle(null_output);
        return false;
    }
    HANDLE inherited[2]{ read, null_output };
    if (!::UpdateProcThreadAttribute(attribute_list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                     inherited, sizeof(inherited), nullptr, nullptr)) {
        error = L"could not restrict the pipe handles: " + wide_error(::GetLastError());
        ::DeleteProcThreadAttributeList(attribute_list);
        ::CloseHandle(write);
        ::CloseHandle(read);
        ::CloseHandle(null_output);
        return false;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = static_cast<DWORD>(sizeof(startup));
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = read;
    startup.StartupInfo.hStdOutput = null_output;
    startup.StartupInfo.hStdError = null_output;
    startup.lpAttributeList = attribute_list;

    PROCESS_INFORMATION process{};
    const BOOL created = ::CreateProcessW(
        shell_.c_str(), command_line.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr, &startup.StartupInfo,
        &process);
    ::DeleteProcThreadAttributeList(attribute_list);
    ::CloseHandle(read);
    ::CloseHandle(null_output);
    if (!created) {
        error = L"could not start \"" + shell_ + L"\": " + wide_error(::GetLastError());
        ::CloseHandle(write);
        return false;
    }
    ::CloseHandle(process.hThread);

    try {
        std::thread writer([write, text = std::move(text), process_handle = process.hProcess]() mutable {
            constexpr size_t kChunk = 32768;
            size_t offset = 0;
            while (offset < text.size()) {
                const DWORD chunk =
                    static_cast<DWORD>(std::min(kChunk, text.size() - offset));
                DWORD written = 0;
                if (!::WriteFile(write, text.data() + offset, chunk, &written, nullptr) ||
                    written == 0) {
                    break;
                }
                offset += written;
            }
            ::CloseHandle(write);
            ::WaitForSingleObject(process_handle, INFINITE);
            ::CloseHandle(process_handle);
        });
        writer.detach();
    } catch (...) {
        ::CloseHandle(write);
        ::CloseHandle(process.hProcess);
        error = L"could not start the pipe writer";
        return false;
    }
    return true;
}

} // namespace termole::app
