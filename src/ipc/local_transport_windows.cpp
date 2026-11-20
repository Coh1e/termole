#include "ipc/local_transport.h"

#include "common/win.h"

#include <sddl.h>

#include <algorithm>
#include <vector>

namespace termole::ipc {
namespace {
struct LocalMemory {
    void operator()(void* value) const noexcept { if (value) ::LocalFree(value); }
};

bool process_user_sid(HANDLE process, std::wstring& sid, std::string& error)
{
    HANDLE token_handle = nullptr;
    if (!::OpenProcessToken(process, TOKEN_QUERY, &token_handle)) {
        error = win::system_error("OpenProcessToken", ::GetLastError()).message;
        return false;
    }
    win::unique_handle token(token_handle);
    DWORD size = 0;
    (void)::GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (!size) {
        error = win::system_error("GetTokenInformation", ::GetLastError()).message;
        return false;
    }
    std::vector<std::byte> data(size);
    if (!::GetTokenInformation(token.get(), TokenUser, data.data(), size, &size)) {
        error = win::system_error("GetTokenInformation", ::GetLastError()).message;
        return false;
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(data.data());
    wchar_t* text = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &text)) {
        error = win::system_error("ConvertSidToStringSidW", ::GetLastError()).message;
        return false;
    }
    std::unique_ptr<wchar_t, LocalMemory> owned(text);
    sid = text;
    return true;
}

bool pipe_identity(std::string_view name, std::wstring& path, std::wstring& sid,
                   DWORD& login, std::string& error)
{
    if (name.empty() || name.size() > 32 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
            std::string_view::npos) {
        error = "endpoint name must have 1–32 letters, digits, hyphens or underscores";
        return false;
    }
    if (!process_user_sid(::GetCurrentProcess(), sid, error)) return false;
    if (!::ProcessIdToSessionId(::GetCurrentProcessId(), &login)) {
        error = win::system_error("ProcessIdToSessionId", ::GetLastError()).message;
        return false;
    }
    path = L"\\\\.\\pipe\\termole-" + sid + L"-" + std::to_wstring(login) + L"-";
    path.append(name.begin(), name.end());
    return true;
}

bool same_server_user(HANDLE pipe, const std::wstring& sid, DWORD login, std::string& error)
{
    ULONG session = 0;
    ULONG process_id = 0;
    if (!::GetNamedPipeServerSessionId(pipe, &session) || session != login ||
        !::GetNamedPipeServerProcessId(pipe, &process_id)) {
        error = "Termole pipe does not belong to the current logon session";
        return false;
    }
    win::unique_handle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
    std::wstring server_sid;
    if (!process || !process_user_sid(process.get(), server_sid, error) || server_sid != sid) {
        error = "Termole pipe does not belong to the current user";
        return false;
    }
    return true;
}

bool finish_io(HANDLE pipe, OVERLAPPED& operation, DWORD& transferred, DWORD timeout)
{
    if (::GetOverlappedResultEx(pipe, &operation, &transferred, timeout, FALSE)) return true;
    if (::GetLastError() == WAIT_TIMEOUT) {
        ::CancelIoEx(pipe, &operation);
        // The operation still borrows the caller's buffer until cancellation completes.
        (void)::GetOverlappedResult(pipe, &operation, &transferred, TRUE);
    }
    return false;
}

bool transfer(HANDLE pipe, std::span<uint8_t> output, std::span<const uint8_t> input, bool write)
{
    if (output.empty() && input.empty()) return true;
    win::unique_handle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) return false;
    while (!output.empty() || !input.empty()) {
        const auto remaining = write ? input.size() : output.size();
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 65536));
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        ::ResetEvent(event.get());
        DWORD count = 0;
        const BOOL completed = write ? ::WriteFile(pipe, input.data(), chunk, &count, &operation)
                                     : ::ReadFile(pipe, output.data(), chunk, &count, &operation);
        if (!completed && (::GetLastError() != ERROR_IO_PENDING ||
                           !finish_io(pipe, operation, count, 4000))) return false;
        if (!count) return false;
        if (write) input = input.subspan(count);
        else output = output.subspan(count);
    }
    return true;
}
} // namespace

LocalHandle connect_local(std::string_view name, std::string& error)
{
    std::wstring path;
    std::wstring sid;
    DWORD login = 0;
    if (!pipe_identity(name, path, sid, login, error)) return invalid_handle;
    win::unique_handle pipe(::CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (pipe.get() == INVALID_HANDLE_VALUE) {
        error = win::system_error("connect to Termole", ::GetLastError()).message;
        return invalid_handle;
    }
    if (!same_server_user(pipe.get(), sid, login, error)) return invalid_handle;
    return pipe.release();
}

bool run_local_listener(std::string_view name, const ServeClient& serve, std::string& error)
{
    std::wstring path;
    std::wstring sid;
    DWORD login = 0;
    if (!pipe_identity(name, path, sid, login, error)) return false;
    const std::wstring sddl = L"D:P(A;;GA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
                                                                &raw, nullptr)) {
        error = win::system_error("create pipe ACL", ::GetLastError()).message;
        return false;
    }
    std::unique_ptr<void, LocalMemory> descriptor(raw);
    SECURITY_ATTRIBUTES security{ sizeof(security), raw, FALSE };
    win::unique_handle pipe(::CreateNamedPipeW(
        path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536, 0, &security));
    if (pipe.get() == INVALID_HANDLE_VALUE) {
        error = win::system_error("create Termole pipe", ::GetLastError()).message;
        return false;
    }
    win::unique_handle event(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!event) {
        error = win::system_error("create Termole pipe event", ::GetLastError()).message;
        return false;
    }
    for (;;) {
        OVERLAPPED operation{};
        operation.hEvent = event.get();
        ::ResetEvent(event.get());
        if (!::ConnectNamedPipe(pipe.get(), &operation)) {
            const DWORD code = ::GetLastError();
            DWORD transferred = 0;
            if (code != ERROR_PIPE_CONNECTED &&
                (code != ERROR_IO_PENDING || !finish_io(pipe.get(), operation, transferred, INFINITE))) {
                error = win::system_error("accept Termole connection", code).message;
                return false;
            }
        }
        ULONG client_login = 0;
        const bool same_login = ::GetNamedPipeClientSessionId(pipe.get(), &client_login) &&
            client_login == login;
        const bool keep = !same_login || serve(pipe.get());
        ::DisconnectNamedPipe(pipe.get());
        if (!keep) return true;
    }
}

void close_local(LocalHandle handle)
{
    ::CloseHandle(handle);
}

bool send_bytes(LocalHandle handle, std::span<const uint8_t> bytes)
{
    return transfer(handle, {}, bytes, true);
}

bool read_bytes(LocalHandle handle, std::span<uint8_t> bytes)
{
    return transfer(handle, bytes, {}, false);
}
} // namespace termole::ipc
