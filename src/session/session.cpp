#include "session/session.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace termole {
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
        value = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        return ::InitializeProcThreadAttributeList(value, count, 0, &size) != FALSE;
    }
};

} // namespace

Session::Session(uint16_t columns, uint16_t rows) : terminal_(columns, rows)
{
    terminal_.set_response_handler([this](std::string_view response) { (void)write_raw(response); });
}

Session::~Session()
{
    try {
        (void)terminate();
    } catch (...) {
        // A destructor reached through the C ABI must never throw.
    }
}

Result<std::shared_ptr<Session>> Session::create(const SessionOptions& options)
{
    if (options.columns != 0 &&
        (options.columns < kMinColumns || options.columns > kMaxColumns)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT,
                                          "columns must be between 1 and 1000"));
    }
    if (options.rows != 0 && (options.rows < kMinRows || options.rows > kMaxRows)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "rows must be between 1 and 1000"));
    }

    const uint16_t columns = options.columns == 0 ? 80 : options.columns;
    const uint16_t rows = options.rows == 0 ? 24 : options.rows;

    std::wstring program = options.program;
    if (program.empty()) {
        program = find_default_shell();
        if (program.empty()) {
            return std::unexpected(
                make_error(TERMOLE_STATUS_NOT_FOUND, "no PowerShell executable was found"));
        }
    }

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
    output_write.reset();

    ProcThreadAttributeList attributes;
    if (!attributes.initialize(1)) {
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

    std::wstring command_line = quote_argument(program);
    if (!options.arguments.empty()) {
        command_line.push_back(L' ');
        command_line += options.arguments;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes.value;
    // A console parent would otherwise hand its own console's standard handles
    // to the pseudoconsole client, and console hosts such as PowerShell would
    // write their banner there instead of into the ConPTY stream. Explicitly
    // clear the inherited standard handles and let the console subsystem
    // attach the client to the pseudoconsole.
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION process_info{};
    const wchar_t* working_directory =
        options.working_directory.empty() ? nullptr : options.working_directory.c_str();
    const BOOL created = ::CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                                          EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
                                          working_directory, &startup.StartupInfo, &process_info);
    if (!created) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return std::unexpected(make_error(TERMOLE_STATUS_NOT_FOUND,
                                              "program not found: " + win::win32_error_message(code)));
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
        ::TerminateProcess(process.get(), 1);
        return std::unexpected(win::system_error("ResumeThread", ::GetLastError()));
    }

    auto session = std::shared_ptr<Session>(new Session(columns, rows));
    session->input_write_ = std::move(input_write);
    session->output_read_ = std::move(output_read);
    session->process_ = std::move(process);
    session->job_ = std::move(job);
    session->pseudo_console_ = std::move(pseudo);

    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) {
        (void)session->terminate();
        return std::unexpected(win::system_error("CreateEventW", ::GetLastError()));
    }
    session->data_event_.reset(event);

    // The reader holds a raw pointer, not a shared_ptr: the Session's
    // destructor joins this thread, and a shared_ptr cycle here would keep the
    // session (and its process tree) alive forever after termole_session_destroy.
    session->reader_ = std::thread([pointer = session.get()] { pointer->read_loop(); });
    return session;
}

void Session::read_loop()
{
    std::array<char, 8192> buffer{};
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ::ReadFile(output_read_.get(), buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &read, nullptr);
        if (!ok || read == 0) break;
        {
            std::lock_guard lock(mutex_);
            decoder_.feed(buffer.data(), read,
                          [this](char32_t codepoint) { terminal_.input(codepoint); });
        }
        if (data_event_) ::SetEvent(data_event_.get());
    }

    if (process_) ::WaitForSingleObject(process_.get(), INFINITE);
    running_.store(false, std::memory_order_release);
    if (data_event_) ::SetEvent(data_event_.get());
}

Result<void> Session::terminate()
{
    std::lock_guard terminate_lock(terminate_mutex_);
    if (stopping_.exchange(true)) return {};

    const bool was_running = running_.exchange(false, std::memory_order_acq_rel);
    {
        std::lock_guard lock(input_mutex_);
        input_write_.reset();
    }

    bool failed = false;
    Error first_error{};
    if (job_) {
        if (!::TerminateJobObject(job_.get(), 1) && was_running) {
            first_error = win::system_error("TerminateJobObject", ::GetLastError());
            failed = true;
        }
    } else if (process_) {
        if (!::TerminateProcess(process_.get(), 1) && was_running) {
            first_error = win::system_error("TerminateProcess", ::GetLastError());
            failed = true;
        }
    }

    if (pseudo_console_) {
        // The reader keeps draining while this closes, avoiding the
        // synchronous-close deadlock described by the ConPTY contract.
        // reset() invokes ClosePseudoConsole exactly once through the deleter.
        pseudo_console_.reset();
    }

    try {
        if (reader_.joinable()) reader_.join();
    } catch (const std::exception& exception) {
        if (!failed) {
            first_error = make_error(TERMOLE_STATUS_SYSTEM_ERROR, exception.what());
            failed = true;
        }
    }

    if (failed) return std::unexpected(std::move(first_error));
    return {};
}

Result<void> Session::write(std::string_view bytes)
{
    return write_raw(bytes);
}

Result<void> Session::write_raw(std::string_view bytes)
{
    if (bytes.empty()) return {};
    std::lock_guard lock(input_mutex_);
    if (!input_write_ || stopping_.load(std::memory_order_acquire)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "session input is closed"));
    }

    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!::WriteFile(input_write_.get(), bytes.data() + offset,
                         static_cast<DWORD>(bytes.size() - offset), &written, nullptr)) {
            return std::unexpected(win::system_error("WriteFile", ::GetLastError()));
        }
        if (written == 0) {
            return std::unexpected(
                make_error(TERMOLE_STATUS_SYSTEM_ERROR, "WriteFile wrote zero bytes"));
        }
        offset += written;
    }
    return {};
}

Result<void> Session::send_key(termole_key key)
{
    const char* bytes = nullptr;
    switch (key) {
    case TERMOLE_KEY_ENTER:
        bytes = "\r";
        break;
    case TERMOLE_KEY_TAB:
        bytes = "\t";
        break;
    case TERMOLE_KEY_BACKSPACE:
        bytes = "\x7f";
        break;
    case TERMOLE_KEY_ESCAPE:
        bytes = "\x1b";
        break;
    case TERMOLE_KEY_ARROW_UP:
        bytes = "\x1b[A";
        break;
    case TERMOLE_KEY_ARROW_DOWN:
        bytes = "\x1b[B";
        break;
    case TERMOLE_KEY_ARROW_RIGHT:
        bytes = "\x1b[C";
        break;
    case TERMOLE_KEY_ARROW_LEFT:
        bytes = "\x1b[D";
        break;
    case TERMOLE_KEY_HOME:
        bytes = "\x1b[H";
        break;
    case TERMOLE_KEY_END:
        bytes = "\x1b[F";
        break;
    case TERMOLE_KEY_DELETE:
        bytes = "\x1b[3~";
        break;
    case TERMOLE_KEY_INSERT:
        bytes = "\x1b[2~";
        break;
    case TERMOLE_KEY_PAGE_UP:
        bytes = "\x1b[5~";
        break;
    case TERMOLE_KEY_PAGE_DOWN:
        bytes = "\x1b[6~";
        break;
    case TERMOLE_KEY_CTRL_C:
        bytes = "\x03";
        break;
    case TERMOLE_KEY_CTRL_D:
        bytes = "\x04";
        break;
    default:
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "unknown key"));
    }

    return write_raw(bytes);
}

Result<void> Session::resize(uint16_t columns, uint16_t rows)
{
    if (columns < kMinColumns || columns > kMaxColumns || rows < kMinRows || rows > kMaxRows) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT,
                                          "columns and rows must be between 1 and 1000"));
    }

    std::lock_guard terminate_lock(terminate_mutex_);
    if (stopping_.load(std::memory_order_acquire)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session is already terminated"));
    }

    {
        std::lock_guard lock(mutex_);
        terminal_.resize(columns, rows);
    }
    if (pseudo_console_) {
        const HRESULT status = ::ResizePseudoConsole(
            static_cast<HPCON>(pseudo_console_.get()),
            COORD{ static_cast<SHORT>(columns), static_cast<SHORT>(rows) });
        if (FAILED(status)) {
            return std::unexpected(win::hresult_error("ResizePseudoConsole", status));
        }
    }
    if (data_event_) ::SetEvent(data_event_.get());
    return {};
}

bool Session::attach_view() noexcept
{
    bool expected = false;
    return view_attached_.compare_exchange_strong(expected, true, std::memory_order_acq_rel);
}

void Session::detach_view() noexcept
{
    view_attached_.store(false, std::memory_order_release);
}

Snapshot Session::snapshot() const
{
    std::lock_guard lock(mutex_);
    return terminal_.snapshot();
}

std::pair<uint16_t, uint16_t> Session::size() const
{
    std::lock_guard lock(mutex_);
    return { terminal_.columns(), terminal_.rows() };
}

std::string Session::screen_text() const
{
    std::lock_guard lock(mutex_);
    return terminal_.text();
}

std::wstring Session::title() const
{
    std::lock_guard lock(mutex_);
    return terminal_.title();
}

} // namespace termole
