#include "session/session.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <map>
#include <string>
#include <string_view>
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
        value = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        return ::InitializeProcThreadAttributeList(value, count, 0, &size) != FALSE;
    }
};

} // namespace

Session::Session(uint16_t columns, uint16_t rows) : terminal_(columns, rows)
{
    terminal_.set_response_handler([this](std::string_view response) { (void)write_raw(response); });
    terminal_.set_title_handler([this](std::wstring_view title) {
        push_event(TERMOLE_EVENT_TITLE_CHANGED, 0, win::wide_to_utf8(title));
    });
    terminal_.set_bell_handler([this] { push_event(TERMOLE_EVENT_BELL); });
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

    auto resolved_program = resolve_program(options.program);
    if (!resolved_program) return std::unexpected(resolved_program.error());
    const std::wstring program = std::move(*resolved_program);

    std::wstring working_directory_path = options.working_directory;
    auto working_directory = validate_working_directory(working_directory_path);
    if (!working_directory) return std::unexpected(working_directory.error());
    if (!working_directory_path.empty()) working_directory_path = absolute_path(working_directory_path);

    auto environment = build_environment_block(options.environment);
    if (!environment) return std::unexpected(environment.error());

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

    std::wstring command_line =
        program.find_first_of(L" \t\"") == std::wstring::npos ? program : quote_argument(program);
    for (const auto& argument : options.arguments) {
        command_line.push_back(L' ');
        command_line += quote_argument(argument);
    }

    DWORD creation_flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED;
    LPVOID environment_pointer = nullptr;
    if (!environment->data.empty()) {
        creation_flags |= CREATE_UNICODE_ENVIRONMENT;
        environment_pointer = environment->data.data();
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
    const wchar_t* working_directory_pointer =
        working_directory_path.empty() ? nullptr : working_directory_path.c_str();
    const BOOL created = ::CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, FALSE,
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
    session->exit_thread_ = std::thread([pointer = session.get()] { pointer->exit_loop(); });
    session->reader_ = std::thread([pointer = session.get()] { pointer->read_loop(); });
    return session;
}

void Session::read_loop()
{
    std::array<char, 8192> buffer{};
    bool read_failed = false;
    DWORD read_error = 0;
    for (;;) {
        DWORD read = 0;
        const BOOL ok = ::ReadFile(output_read_.get(), buffer.data(),
                                   static_cast<DWORD>(buffer.size()), &read, nullptr);
        if (!ok) {
            read_error = ::GetLastError();
            if (read_error != ERROR_BROKEN_PIPE && read_error != ERROR_INVALID_HANDLE) {
                read_failed = true;
            }
            break;
        }
        if (read == 0) break;
        {
            std::lock_guard lock(mutex_);
            decoder_.feed(buffer.data(), read,
                          [this](char32_t codepoint) { terminal_.input(codepoint); });
        }
        if (data_event_) ::SetEvent(data_event_.get());
    }

    if (process_) ::WaitForSingleObject(process_.get(), INFINITE);
    DWORD process_exit_code = 0;
    if (process_) ::GetExitCodeProcess(process_.get(), &process_exit_code);
    exit_code_.store(static_cast<int32_t>(process_exit_code), std::memory_order_release);
    running_.store(false, std::memory_order_release);
    if (read_failed) {
        push_event(TERMOLE_EVENT_ERROR, 0,
                   win::system_error("ReadFile", read_error).message);
    }
    push_event(TERMOLE_EVENT_EXITED, static_cast<int32_t>(process_exit_code));
    if (data_event_) ::SetEvent(data_event_.get());
}

void Session::exit_loop()
{
    if (process_) ::WaitForSingleObject(process_.get(), INFINITE);

    // The client has exited. Closing the pseudoconsole forces the output pipe to
    // EOF and lets the reader flush the child's final bytes before running()
    // becomes false. running_ is owned by read_loop so completion is only
    // reported after the output is drained.
    std::lock_guard lock(pseudo_mutex_);
    pseudo_console_.reset();
}

void Session::push_event(termole_event_type type, int32_t exit_code, std::string text)
{
    constexpr size_t kMaxEventQueue = 256;
    std::lock_guard lock(event_mutex_);

    if (type == TERMOLE_EVENT_TITLE_CHANGED && !events_.empty() &&
        events_.back().type == TERMOLE_EVENT_TITLE_CHANGED) {
        events_.back().text = std::move(text);
    } else {
        if (events_.size() >= kMaxEventQueue) {
            const auto disposable = std::find_if(
                events_.begin(), events_.end(), [](const EventRecord& event) {
                    return event.type != TERMOLE_EVENT_EXITED && event.type != TERMOLE_EVENT_ERROR;
                });
            if (disposable != events_.end()) events_.erase(disposable);
            else events_.pop_front();
        }
        events_.push_back(EventRecord{ type, exit_code, std::move(text) });
    }
    if (data_event_) ::SetEvent(data_event_.get());
}

bool Session::poll_event(EventRecord& out)
{
    std::lock_guard lock(event_mutex_);
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

Result<int32_t> Session::exit_code() const
{
    if (running()) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session is still running"));
    }
    return exit_code_.load(std::memory_order_acquire);
}

std::string Session::scrollback_text() const
{
    std::lock_guard lock(mutex_);
    return terminal_.scrollback_text();
}

size_t Session::scrollback_line_count() const
{
    std::lock_guard lock(mutex_);
    return terminal_.scrollback_line_count();
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
        if (!::TerminateJobObject(job_.get(), 1)) {
            const DWORD job_error = ::GetLastError();
            // Keep the direct-child guarantee even if the job call fails.
            if (process_) ::TerminateProcess(process_.get(), 1);
            if (was_running) {
                first_error = win::system_error("TerminateJobObject", job_error);
                failed = true;
            }
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
        std::lock_guard lock(pseudo_mutex_);
        pseudo_console_.reset();
    }

    try {
        if (reader_.joinable()) reader_.join();
        if (exit_thread_.joinable()) exit_thread_.join();
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
    const bool application = application_cursor();
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
        bytes = application ? "\x1bOA" : "\x1b[A";
        break;
    case TERMOLE_KEY_ARROW_DOWN:
        bytes = application ? "\x1bOB" : "\x1b[B";
        break;
    case TERMOLE_KEY_ARROW_RIGHT:
        bytes = application ? "\x1bOC" : "\x1b[C";
        break;
    case TERMOLE_KEY_ARROW_LEFT:
        bytes = application ? "\x1bOD" : "\x1b[D";
        break;
    case TERMOLE_KEY_HOME:
        bytes = application ? "\x1bOH" : "\x1b[H";
        break;
    case TERMOLE_KEY_END:
        bytes = application ? "\x1bOF" : "\x1b[F";
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
        std::lock_guard lock(pseudo_mutex_);
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

Snapshot Session::snapshot(int scroll_offset) const
{
    std::lock_guard lock(mutex_);
    return terminal_.snapshot(scroll_offset);
}

std::string Session::selection_text(int start_row, int start_column, int end_row,
                                    int end_column) const
{
    std::lock_guard lock(mutex_);
    return terminal_.selection_text(start_row, start_column, end_row, end_column);
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

uint8_t Session::mouse_mode() const
{
    std::lock_guard lock(mutex_);
    return terminal_.mouse_mode();
}

bool Session::sgr_mouse() const
{
    std::lock_guard lock(mutex_);
    return terminal_.sgr_mouse();
}

bool Session::bracketed_paste() const
{
    std::lock_guard lock(mutex_);
    return terminal_.bracketed_paste();
}

bool Session::application_cursor() const
{
    std::lock_guard lock(mutex_);
    return terminal_.application_cursor();
}

} // namespace termole
