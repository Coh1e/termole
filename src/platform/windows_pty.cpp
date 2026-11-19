#include "platform/windows_pty.h"

#include <vector>

namespace termole::platform {

namespace {

std::wstring widen(const std::string& text)
{
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                          length);
    return result;
}

std::wstring quote(const std::wstring& argument)
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

struct AttributeList {
    std::vector<std::byte> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;

    ~AttributeList()
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

std::unique_ptr<Pty> WindowsPty::spawn(const PtyOptions& options, std::string& error)
{
    if (options.arguments.empty()) {
        error = "windows pty requires a program";
        return nullptr;
    }

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE input_read = nullptr;
    HANDLE input_write = nullptr;
    HANDLE output_read = nullptr;
    HANDLE output_write = nullptr;
    if (!::CreatePipe(&input_read, &input_write, &security, 0)) {
        error = "CreatePipe failed";
        return nullptr;
    }
    if (!::CreatePipe(&output_read, &output_write, &security, 0)) {
        ::CloseHandle(input_read);
        ::CloseHandle(input_write);
        error = "CreatePipe failed";
        return nullptr;
    }
    ::SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

    HPCON console = nullptr;
    const HRESULT status = ::CreatePseudoConsole(
        COORD{ static_cast<SHORT>(options.columns), static_cast<SHORT>(options.rows) }, input_read,
        output_write, 0, &console);
    if (FAILED(status) || !console) {
        ::CloseHandle(input_read);
        ::CloseHandle(input_write);
        ::CloseHandle(output_read);
        ::CloseHandle(output_write);
        error = "CreatePseudoConsole failed";
        return nullptr;
    }
    ::CloseHandle(input_read);
    ::CloseHandle(output_write);

    AttributeList attributes;
    if (!attributes.initialize(1)) {
        ::ClosePseudoConsole(console);
        ::CloseHandle(input_write);
        ::CloseHandle(output_read);
        error = "InitializeProcThreadAttributeList failed";
        return nullptr;
    }
    if (!::UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                     console, sizeof(HPCON), nullptr, nullptr)) {
        ::ClosePseudoConsole(console);
        ::CloseHandle(input_write);
        ::CloseHandle(output_read);
        error = "UpdateProcThreadAttribute failed";
        return nullptr;
    }

    const auto argument_text = [](const std::string& value) {
        const std::wstring widened = widen(value);
        if (!widened.empty() && widened.find_first_of(L" \t\"") == std::wstring::npos)
            return widened;
        return quote(widened);
    };
    std::wstring program = widen(options.arguments.front());
    std::wstring command = argument_text(options.arguments.front());
    for (size_t index = 1; index < options.arguments.size(); ++index) {
        command.push_back(L' ');
        command += argument_text(options.arguments[index]);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes.value;
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdOutput = INVALID_HANDLE_VALUE;
    startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process{};
    const std::wstring working_directory = widen(options.working_directory);
    const BOOL created = ::CreateProcessW(
        nullptr, command.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(), &startup.StartupInfo,
        &process);
    if (!created) {
        ::ClosePseudoConsole(console);
        ::CloseHandle(input_write);
        ::CloseHandle(output_read);
        error = "CreateProcessW failed: " + std::to_string(::GetLastError());
        return nullptr;
    }

    HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                       sizeof(limits)) ||
            !::AssignProcessToJobObject(job, process.hProcess)) {
            ::CloseHandle(job);
            job = nullptr;
        }
    }

    if (::ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        ::TerminateProcess(process.hProcess, 1);
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        if (job) ::CloseHandle(job);
        ::ClosePseudoConsole(console);
        ::CloseHandle(input_write);
        ::CloseHandle(output_read);
        error = "ResumeThread failed";
        return nullptr;
    }
    ::CloseHandle(process.hThread);

    std::unique_ptr<WindowsPty> pty(new WindowsPty());
    pty->input_write_ = input_write;
    pty->output_read_ = output_read;
    pty->process_ = process.hProcess;
    pty->job_ = job;
    pty->console_ = console;
    return pty;
}

WindowsPty::~WindowsPty()
{
    close();
}

ssize_t WindowsPty::read(void* buffer, size_t capacity)
{
    if (!output_read_) return -1;
    DWORD read = 0;
    if (!::ReadFile(output_read_, buffer, static_cast<DWORD>(capacity), &read, nullptr)) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_INVALID_HANDLE) return 0;
        return -1;
    }
    return static_cast<ssize_t>(read);
}

ssize_t WindowsPty::write(const void* data, size_t size)
{
    if (!input_write_) return -1;
    DWORD written = 0;
    if (!::WriteFile(input_write_, data, static_cast<DWORD>(size), &written, nullptr)) return -1;
    return static_cast<ssize_t>(written);
}

bool WindowsPty::resize(unsigned short columns, unsigned short rows, std::string& error)
{
    if (!console_) {
        error = "ConPTY is closed";
        return false;
    }
    const HRESULT status = ::ResizePseudoConsole(
        console_, COORD{ static_cast<SHORT>(columns), static_cast<SHORT>(rows) });
    if (FAILED(status)) {
        error = "ResizePseudoConsole failed";
        return false;
    }
    return true;
}

int WindowsPty::wait()
{
    if (!process_) return -1;
    ::WaitForSingleObject(process_, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(process_, &code);
    return static_cast<int>(code);
}

void WindowsPty::close()
{
    if (closed_) return;
    closed_ = true;
    if (input_write_) {
        ::CloseHandle(input_write_);
        input_write_ = nullptr;
    }
    if (console_) {
        ::ClosePseudoConsole(console_);
        console_ = nullptr;
    }
    if (output_read_) {
        ::CloseHandle(output_read_);
        output_read_ = nullptr;
    }
    if (job_) {
        ::CloseHandle(job_);
        job_ = nullptr;
    }
    if (process_) {
        ::CloseHandle(process_);
        process_ = nullptr;
    }
}

} // namespace termole::platform
