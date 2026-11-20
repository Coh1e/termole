#include "platform/windows_pty.h"

#include "common/limits.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <limits>
#include <map>

namespace termole::platform {
namespace {

std::wstring quote_argument(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(backslashes * (character == L'"' ? 2 : 1), L'\\');
        backslashes = 0;
        if (character == L'"') result.push_back(L'\\');
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

// cmd.exe re-parses its own command line instead of using argv, so adding
// quotes that are not required silently changes what programs such as
// `cmd /c ...` execute. Quote only tokens that would otherwise be split or
// lose characters, and leave plain tokens untouched like a shell would.
bool needs_quoting(const std::wstring& argument)
{
    return argument.empty() || argument.find_first_of(L" \t\"") != std::wstring::npos;
}

bool file_exists(const std::wstring& path)
{
    if (path.empty()) return false;
    const DWORD attributes = ::GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring environment_variable(const wchar_t* name)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length =
        ::GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring search_path(const wchar_t* executable)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::SearchPathW(nullptr, executable, nullptr, static_cast<DWORD>(buffer.size()),
                                       buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return {};
    return std::wstring(buffer.data(), length);
}

std::wstring find_default_shell()
{
    std::vector<std::wstring> candidates;
    for (const wchar_t* variable : { L"ProgramFiles", L"ProgramW6432" }) {
        const std::wstring root = environment_variable(variable);
        if (!root.empty()) candidates.push_back(root + L"\\PowerShell\\7\\pwsh.exe");
    }

    const std::wstring local = environment_variable(L"LOCALAPPDATA");
    if (!local.empty()) candidates.push_back(local + L"\\Microsoft\\WindowsApps\\pwsh.exe");

    for (const auto& candidate : candidates) {
        if (file_exists(candidate)) return candidate;
    }

    const std::wstring pwsh = search_path(L"pwsh.exe");
    if (!pwsh.empty()) return pwsh;

    std::array<wchar_t, 32768> system_directory{};
    const UINT system_length =
        ::GetSystemDirectoryW(system_directory.data(), static_cast<UINT>(system_directory.size()));
    if (system_length > 0 && system_length < system_directory.size()) {
        const std::wstring powershell =
            std::wstring(system_directory.data(), system_length) +
            L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        if (file_exists(powershell)) return powershell;
    }

    return search_path(L"powershell.exe");
}

bool path_has_separator(const std::wstring& path)
{
    return path.find_first_of(L"\\/") != std::wstring::npos ||
        (path.size() >= 2 && path[1] == L':');
}

std::wstring absolute_path(const std::wstring& path)
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
                                            buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) return path;
    return std::wstring(buffer.data(), length);
}

Result<std::wstring> resolve_program(const std::wstring& requested)
{
    if (requested.empty()) {
        const std::wstring shell = find_default_shell();
        if (shell.empty()) {
            return std::unexpected(
                make_error(TERMOLE_STATUS_NOT_FOUND, "no PowerShell executable was found"));
        }
        return shell;
    }

    if (path_has_separator(requested)) {
        const std::wstring full_path = absolute_path(requested);
        if (!file_exists(full_path)) {
            return std::unexpected(make_error(
                TERMOLE_STATUS_NOT_FOUND, "program not found: " + win::wide_to_utf8(full_path)));
        }
        return full_path;
    }

    std::array<wchar_t, 32768> buffer{};
    const wchar_t* extension = requested.find(L'.') == std::wstring::npos ? L".exe" : nullptr;
    const DWORD length = ::SearchPathW(nullptr, requested.c_str(), extension,
                                       static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) {
        return std::unexpected(make_error(
            TERMOLE_STATUS_NOT_FOUND, "program not found: " + win::wide_to_utf8(requested)));
    }
    return std::wstring(buffer.data(), length);
}

// CreateProcessW leaks Windows-internal handles when the image cannot be
// opened (for example while another process holds it without sharing). A
// read-open preflight with the loader's share mode catches that class before
// the pseudoconsole or pipes exist, so a host retrying a bad launch does not
// accumulate handles. The diagnostic keeps the CreateProcessW prefix because
// this is still the program-start failure surface.
//
// An App Execution Alias (for example wt.exe or winget.exe) is a reparse
// point that cannot be opened for read at all, yet CreateProcessW resolves
// and launches it. Those aliases carry IO_REPARSE_TAG_APPEXECLINK. Every
// other image, including a symlink to a locked file, keeps the preflight
// guard, so the failed-launch leak class stays covered.
bool is_app_execution_alias(const std::wstring& program)
{
    WIN32_FIND_DATAW data{};
    const HANDLE find = ::FindFirstFileW(program.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    ::FindClose(find);
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
        data.dwReserved0 == IO_REPARSE_TAG_APPEXECLINK;
}

Result<void> preflight_program_image(const std::wstring& program)
{
    win::unique_handle image(::CreateFileW(program.c_str(), GENERIC_READ,
                                          FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (image.get() != INVALID_HANDLE_VALUE) return {};

    const DWORD error = ::GetLastError();
    if (is_app_execution_alias(program)) return {};
    return std::unexpected(win::system_error("CreateProcessW preflight", error));
}

Result<void> validate_working_directory(const std::wstring& directory)
{
    if (directory.empty()) return {};
    const DWORD attributes = ::GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return std::unexpected(make_error(
            TERMOLE_STATUS_INVALID_ARGUMENT,
            "working directory is not a directory: " + win::wide_to_utf8(directory)));
    }
    return {};
}

struct CaseInsensitiveLess {
    bool operator()(const std::wstring& left, const std::wstring& right) const noexcept
    {
        return ::_wcsicmp(left.c_str(), right.c_str()) < 0;
    }
};

struct EnvironmentBlock {
    std::vector<wchar_t> data;
};

Result<EnvironmentBlock> build_environment_block(const std::vector<std::wstring>& overrides)
{
    EnvironmentBlock block;
    if (overrides.empty()) return block;

    wchar_t* raw = ::GetEnvironmentStringsW();
    if (!raw) {
        return std::unexpected(win::system_error("GetEnvironmentStringsW", ::GetLastError()));
    }
    struct EnvironmentStringsGuard {
        wchar_t* value;
        ~EnvironmentStringsGuard()
        {
            if (value) ::FreeEnvironmentStringsW(value);
        }
    } guard{ raw };

    std::map<std::wstring, std::wstring, CaseInsensitiveLess> values;
    for (const wchar_t* cursor = raw; *cursor != L'\0';) {
        const std::wstring_view entry(cursor);
        const size_t separator =
            entry[0] == L'=' ? entry.find(L'=', 1) : entry.find(L'=');
        if (separator != std::wstring_view::npos) {
            values[std::wstring(entry.substr(0, separator))] =
                std::wstring(entry.substr(separator + 1));
        }
        cursor += entry.size() + 1;
    }

    for (const auto& override_entry : overrides) {
        const size_t separator = override_entry.find(L'=');
        if (separator == std::wstring::npos || separator == 0 || override_entry[0] == L'=') {
            return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT,
                                              "environment entries must be KEY=VALUE"));
        }
        values[override_entry.substr(0, separator)] = override_entry.substr(separator + 1);
    }

    size_t total = 1;
    for (const auto& [key, value] : values) total += key.size() + value.size() + 2;
    block.data.reserve(total);
    for (const auto& [key, value] : values) {
        block.data.insert(block.data.end(), key.begin(), key.end());
        block.data.push_back(L'=');
        block.data.insert(block.data.end(), value.begin(), value.end());
        block.data.push_back(L'\0');
    }
    block.data.push_back(L'\0');
    return block;
}

struct ProcThreadAttributeList {
    std::vector<std::byte> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;

    ~ProcThreadAttributeList()
    {
        if (value) ::DeleteProcThreadAttributeList(value);
    }

    bool initialize(DWORD count)
    {
        SIZE_T size = 0;
        ::InitializeProcThreadAttributeList(nullptr, count, 0, &size);
        if (size == 0) return false;
        storage.resize(size);
        auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!::InitializeProcThreadAttributeList(list, count, 0, &size)) return false;
        value = list;
        return true;
    }
};

} // namespace

std::unique_ptr<Pty> WindowsPty::spawn(const PtyOptions& options, std::string& error)
{
    if (options.arguments.empty() || options.arguments.front().empty()) {
        error = "windows pty requires a program";
        return nullptr;
    }

    const auto widen = [](std::string_view text) -> Result<std::wstring> {
        if (text.empty()) return std::wstring{};
        if (text.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "PTY argument is too long"));
        }
        const int size = static_cast<int>(text.size());
        const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size,
                                                 nullptr, 0);
        if (length == 0)
            return std::unexpected(win::system_error("MultiByteToWideChar", ::GetLastError()));
        std::wstring result(static_cast<size_t>(length), L'\0');
        ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size, result.data(), length);
        return result;
    };

    WindowsPtyOptions native;
    native.columns = options.columns;
    native.rows = options.rows;
    auto program = widen(options.arguments.front());
    auto directory = widen(options.working_directory);
    if (!program || !directory) {
        error = (!program ? program.error() : directory.error()).message;
        return nullptr;
    }
    native.program = std::move(*program);
    native.working_directory = std::move(*directory);
    for (size_t index = 1; index < options.arguments.size(); ++index) {
        auto argument = widen(options.arguments[index]);
        if (!argument) {
            error = argument.error().message;
            return nullptr;
        }
        native.arguments.push_back(std::move(*argument));
    }

    auto pty = spawn_native(native);
    if (!pty) {
        error = std::move(pty.error().message);
        return nullptr;
    }
    return std::move(*pty);
}

Result<std::unique_ptr<WindowsPty>> WindowsPty::spawn_native(const WindowsPtyOptions& options)
{
    const uint16_t columns = options.columns == 0 ? 80 : options.columns;
    const uint16_t rows = options.rows == 0 ? 24 : options.rows;
    if (columns > kMaxColumns || rows > kMaxRows) {
        return std::unexpected(make_error(
            TERMOLE_STATUS_INVALID_ARGUMENT, "columns and rows must be between 1 and 1000"));
    }
    auto pty = std::unique_ptr<WindowsPty>(new WindowsPty());

    const bool file_redirect = !options.stdin_path.empty() || !options.stdout_path.empty();
    const bool handle_redirect = options.stdin_handle && options.stdout_handle;
    if (static_cast<bool>(options.stdin_handle) != static_cast<bool>(options.stdout_handle)) {
        return std::unexpected(make_error(
            TERMOLE_STATUS_INVALID_ARGUMENT, "both stdin and stdout handles are required"));
    }
    if (file_redirect && handle_redirect) {
        return std::unexpected(make_error(
            TERMOLE_STATUS_INVALID_ARGUMENT,
            "file redirection and direct standard handles are mutually exclusive"));
    }
    if (file_redirect && (options.stdin_path.empty() || options.stdout_path.empty())) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "both stdin and stdout paths are required"));
    }

    auto resolved_program = resolve_program(options.program);
    if (!resolved_program) return std::unexpected(resolved_program.error());
    const std::wstring program = std::move(*resolved_program);

    std::wstring working_directory_path = options.working_directory;
    auto working_directory = validate_working_directory(working_directory_path);
    if (!working_directory) return std::unexpected(working_directory.error());
    if (!working_directory_path.empty()) working_directory_path = absolute_path(working_directory_path);

    auto environment = build_environment_block(options.environment);
    if (!environment) return std::unexpected(environment.error());

    auto readable_program = preflight_program_image(program);
    if (!readable_program) return std::unexpected(readable_program.error());

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE input_read_raw = nullptr;
    HANDLE input_write_raw = nullptr;
    HANDLE output_read_raw = nullptr;
    HANDLE output_write_raw = nullptr;
    if (!::CreatePipe(&input_read_raw, &input_write_raw, &security, 0))
        return std::unexpected(win::system_error("CreatePipe", ::GetLastError()));
    if (!::CreatePipe(&output_read_raw, &output_write_raw, &security, 0)) {
        ::CloseHandle(input_read_raw);
        ::CloseHandle(input_write_raw);
        return std::unexpected(win::system_error("CreatePipe", ::GetLastError()));
    }

    win::unique_handle input_read(input_read_raw);
    win::unique_handle input_write(input_write_raw);
    win::unique_handle output_read(output_read_raw);
    win::unique_handle output_write(output_write_raw);

    ::SetHandleInformation(input_write.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(output_read.get(), HANDLE_FLAG_INHERIT, 0);

    HPCON pseudo_raw = nullptr;
    const HRESULT pseudo_status = ::CreatePseudoConsole(
        COORD{ static_cast<SHORT>(columns), static_cast<SHORT>(rows) }, input_read.get(),
        output_write.get(), 0, &pseudo_raw);
    if (FAILED(pseudo_status) || pseudo_raw == nullptr) {
        return std::unexpected(win::hresult_error("CreatePseudoConsole", pseudo_status));
    }
    win::unique_pseudo_console pseudo(pseudo_raw);
    input_read.reset();
    // In handle-redirect mode the child's stderr is wired to the ConPTY output
    // stream, so the terminal keeps stderr/UI while stdin/stdout carry raw data.
    if (handle_redirect) {
        ::SetHandleInformation(output_write.get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    } else {
        output_write.reset();
    }

    ProcThreadAttributeList attributes;
    if (!attributes.initialize(2)) {
        return std::unexpected(
            win::system_error("InitializeProcThreadAttributeList", ::GetLastError()));
    }
    if (!::UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     pseudo.get(), sizeof(HPCON), nullptr, nullptr)) {
        return std::unexpected(win::system_error("UpdateProcThreadAttribute", ::GetLastError()));
    }

    win::unique_handle job(::CreateJobObjectW(nullptr, nullptr));
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits,
                                       sizeof(limits))) {
            job.reset();
        }
    }

    std::wstring command_line =
        program.find_first_of(L" \t\"") == std::wstring::npos ? program : quote_argument(program);
    for (const auto& argument : options.arguments) {
        command_line.push_back(L' ');
        command_line += needs_quoting(argument) ? quote_argument(argument) : argument;
    }

    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
    LPVOID environment_pointer = nullptr;
    if (!environment->data.empty()) {
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
        environment_pointer = environment->data.data();
    }

    win::unique_handle redirected_input;
    win::unique_handle redirected_output;
    if (file_redirect) {
        SECURITY_ATTRIBUTES redirect_security{ sizeof(redirect_security), nullptr, TRUE };
        redirected_input.reset(::CreateFileW(options.stdin_path.c_str(), GENERIC_READ,
                                             FILE_SHARE_READ, &redirect_security, OPEN_EXISTING,
                                             FILE_ATTRIBUTE_NORMAL, nullptr));
        if (redirected_input.get() == INVALID_HANDLE_VALUE) {
            return std::unexpected(win::system_error("CreateFileW(stdin)", ::GetLastError()));
        }
        redirected_output.reset(::CreateFileW(options.stdout_path.c_str(), GENERIC_WRITE,
                                              FILE_SHARE_READ, &redirect_security, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr));
        if (redirected_output.get() == INVALID_HANDLE_VALUE) {
            return std::unexpected(win::system_error("CreateFileW(stdout)", ::GetLastError()));
        }
    }

    HANDLE child_error = INVALID_HANDLE_VALUE;
    HANDLE inherited_handles[3]{};
    if (handle_redirect) {
        // Pass the same streams to the child without changing the caller's
        // inheritance flags. stderr remains in the ConPTY output stream.
        const HANDLE current = ::GetCurrentProcess();
        HANDLE input = nullptr;
        if (!::DuplicateHandle(current, options.stdin_handle, current, &input,
                               0, TRUE, DUPLICATE_SAME_ACCESS))
            return std::unexpected(win::system_error("DuplicateHandle(stdin)", ::GetLastError()));
        redirected_input.reset(input);
        HANDLE output = nullptr;
        if (!::DuplicateHandle(current, options.stdout_handle, current, &output,
                               0, TRUE, DUPLICATE_SAME_ACCESS))
            return std::unexpected(win::system_error("DuplicateHandle(stdout)", ::GetLastError()));
        redirected_output.reset(output);
        child_error = output_write.get();
    }
    const bool redirect = handle_redirect || file_redirect;
    if (redirect) {
        inherited_handles[0] = redirected_input.get();
        inherited_handles[1] = redirected_output.get();
        inherited_handles[2] = child_error;
        const size_t handle_count = handle_redirect ? 3 : 2;
        if (!::UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                         inherited_handles, handle_count * sizeof(HANDLE),
                                         nullptr, nullptr)) {
            return std::unexpected(
                win::system_error("UpdateProcThreadAttribute(handles)", ::GetLastError()));
        }
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes.value;
    // A console parent would otherwise hand its own console's standard handles
    // to the pseudoconsole client, and console hosts such as PowerShell would
    // write their banner there instead of into the ConPTY stream. Clear the
    // inherited handles and let the console subsystem attach the client to the
    // pseudoconsole, unless the caller explicitly redirects data handles.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = redirect ? redirected_input.get() : INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = redirect ? redirected_output.get() : INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdError = redirect ? child_error : INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION process_info{};
    const wchar_t* working_directory_pointer =
        working_directory_path.empty() ? nullptr : working_directory_path.c_str();
    const BOOL created = ::CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr,
                                          redirect ? TRUE : FALSE,
                                          creation_flags, environment_pointer, working_directory_pointer,
                                          &startup.StartupInfo, &process_info);
    if (!created) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return std::unexpected(make_error(TERMOLE_STATUS_NOT_FOUND,
                                              "program not found: " + win::win32_error_message(code)));
        }
        if (code == ERROR_DIRECTORY) {
            return std::unexpected(
                make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "working directory is not valid"));
        }
        return std::unexpected(win::system_error("CreateProcessW", code));
    }

    win::unique_handle process(process_info.hProcess);
    win::unique_handle thread(process_info.hThread);

    if (job) {
        // Nested jobs are supported on the target platforms. If a host policy
        // prevents assignment, keep the session usable and fall back to
        // terminating the direct child.
        if (!::AssignProcessToJobObject(job.get(), process.get())) job.reset();
    }

    if (::ResumeThread(thread.get()) == static_cast<DWORD>(-1)) {
        const DWORD error = ::GetLastError();
        ::TerminateProcess(process.get(), 1);
        return std::unexpected(win::system_error("ResumeThread", error));
    }

    pty->input_write_ = std::move(input_write);
    pty->output_read_ = std::move(output_read);
    pty->process_ = std::move(process);
    pty->job_ = std::move(job);
    pty->pseudo_console_ = std::move(pseudo);
    if (handle_redirect) pty->console_output_write_ = std::move(output_write);
    return pty;
}

WindowsPty::~WindowsPty()
{
    close();
}

ssize_t WindowsPty::read(void* buffer, size_t capacity)
{
    const auto result = read_output(buffer, capacity);
    return result ? static_cast<ssize_t>(*result) : -1;
}

Result<size_t> WindowsPty::read_output(void* buffer, size_t capacity)
{
    DWORD count = 0;
    const auto chunk = static_cast<DWORD>(
        std::min(capacity, static_cast<size_t>(std::numeric_limits<DWORD>::max())));
    if (!::ReadFile(output_read_.get(), buffer, chunk, &count, nullptr)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE) return 0;
        return std::unexpected(win::system_error("ReadFile", error));
    }
    return count;
}

ssize_t WindowsPty::write(const void* data, size_t size)
{
    const auto result = write_input(std::string_view(static_cast<const char*>(data), size));
    return result ? static_cast<ssize_t>(size) : -1;
}

Result<void> WindowsPty::write_input(std::string_view bytes)
{
    if (bytes.empty()) return {};
    std::lock_guard lock(input_mutex_);
    if (!input_write_) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "session input is closed"));
    }

    while (!bytes.empty()) {
        DWORD count = 0;
        const auto chunk = static_cast<DWORD>(
            std::min(bytes.size(), static_cast<size_t>(std::numeric_limits<DWORD>::max())));
        if (!::WriteFile(input_write_.get(), bytes.data(), chunk, &count, nullptr)) {
            return std::unexpected(win::system_error("WriteFile", ::GetLastError()));
        }
        if (count == 0) {
            return std::unexpected(
                make_error(TERMOLE_STATUS_SYSTEM_ERROR, "WriteFile wrote zero bytes"));
        }
        bytes.remove_prefix(count);
    }
    return {};
}

bool WindowsPty::resize(unsigned short columns, unsigned short rows, std::string& error)
{
    const auto result = resize_console(columns, rows);
    if (!result) error = result.error().message;
    return result.has_value();
}

Result<void> WindowsPty::resize_console(uint16_t columns, uint16_t rows)
{
    std::lock_guard lock(pseudo_mutex_);
    if (!pseudo_console_) return {};
    const HRESULT status = ::ResizePseudoConsole(
        static_cast<HPCON>(pseudo_console_.get()),
        COORD{ static_cast<SHORT>(columns), static_cast<SHORT>(rows) });
    if (FAILED(status)) {
        return std::unexpected(win::hresult_error("ResizePseudoConsole", status));
    }
    return {};
}

int WindowsPty::wait()
{
    return wait_for_exit();
}

int32_t WindowsPty::wait_for_exit() const
{
    DWORD code = 0;
    if (process_) {
        ::WaitForSingleObject(process_.get(), INFINITE);
        ::GetExitCodeProcess(process_.get(), &code);
    }
    return static_cast<int32_t>(code);
}

void WindowsPty::close_console()
{
    std::lock_guard lock(pseudo_mutex_);
    console_output_write_.reset();
    pseudo_console_.reset();
}

Result<void> WindowsPty::terminate_process(bool was_running)
{
    if (job_) {
        if (!::TerminateJobObject(job_.get(), 1)) {
            const DWORD error = ::GetLastError();
            if (process_) ::TerminateProcess(process_.get(), 1);
            if (was_running) return std::unexpected(win::system_error("TerminateJobObject", error));
        }
    } else if (process_ && !::TerminateProcess(process_.get(), 1) && was_running) {
        return std::unexpected(win::system_error("TerminateProcess", ::GetLastError()));
    }
    return {};
}

void WindowsPty::close_input()
{
    std::lock_guard lock(input_mutex_);
    input_write_.reset();
}

void WindowsPty::close()
{
    (void)terminate_process(false);
    close_input();
    close_console();
    output_read_.reset();
    job_.reset();
    process_.reset();
}

} // namespace termole::platform
