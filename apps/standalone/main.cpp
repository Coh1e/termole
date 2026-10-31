#include "common/cli.h"
#include "common/console.h"
#include "common/diagnostics.h"
#include "common/exit_codes.h"
#include "standalone/gui.h"

#include <windows.h>

#include <string>
#include <string_view>

namespace {

// Re-quotes one already-parsed argv token so the GUI role's parser returns
// exactly the same token. Unlike Session's child command line (which quotes
// only when required, because cmd.exe re-parses it), the launcher hands these
// tokens to our own CommandLineToArgvW-compatible parser, so quoting all of
// them is unambiguous and preserves every token.
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

std::wstring module_path()
{
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return buffer;
}

void report_error(const std::wstring& message)
{
    const std::wstring text = L"termole: " + message + L"\n";
    if (!termole::app::write_standard_error(text)) {
        termole::app::show_message(text + L"\nUse 'termole --help' for usage.\n", true);
    }
}

bool valid_handle(HANDLE handle)
{
    return handle && handle != INVALID_HANDLE_VALUE;
}

HANDLE inheritable_stream(DWORD standard_handle)
{
    if (!termole::app::standard_stream_is_redirected(standard_handle)) {
        return INVALID_HANDLE_VALUE;
    }
    const HANDLE handle = ::GetStdHandle(standard_handle);
    ::SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return handle;
}

// Installs the caller's redirected standard handles on the GUI role. Console
// handles are deliberately skipped: the GUI role is detached from every
// console, so a caller console closing (or Ctrl+C) cannot terminate the
// terminal window. Console diagnostics still work through AttachConsole on the
// launcher while it waits.
bool install_standard_handles(STARTUPINFOW& startup)
{
    const HANDLE input = inheritable_stream(STD_INPUT_HANDLE);
    const HANDLE output = inheritable_stream(STD_OUTPUT_HANDLE);
    const HANDLE error = inheritable_stream(STD_ERROR_HANDLE);
    if (!valid_handle(input) && !valid_handle(output) && !valid_handle(error)) {
        return false;
    }
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = error;
    return true;
}

// Starts the same executable in its internal GUI role. Returns the process
// handle (caller closes it) or nullptr on failure.
HANDLE start_gui_role(int argc, wchar_t** argv, bool pass_standard_handles)
{
    std::wstring command = quote_argument(module_path()) + L" --gui";
    for (int index = 1; index < argc; ++index) {
        command.push_back(L' ');
        command += quote_argument(argv[index]);
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    BOOL inherit = FALSE;
    if (pass_standard_handles) inherit = install_standard_handles(startup) ? TRUE : FALSE;

    // The GUI role is always detached from a console: it owns a window, not a
    // terminal session, and must survive the caller's console window.
    const DWORD flags = CREATE_NO_WINDOW;

    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, inherit, flags, nullptr,
                          nullptr, &startup, &process)) {
        const DWORD code = ::GetLastError();
        report_error(L"could not start the terminal window: " +
                     termole::app::win32_error_message(code));
        return nullptr;
    }
    ::CloseHandle(process.hThread);
    return process.hProcess;
}

int run_launcher(int argc, wchar_t** argv)
{
    // A console allocated for this launch (Explorer or CREATE_NEW_CONSOLE) must
    // not survive as a blank window. Detect it first, but hide it only once we
    // know the command is valid, so --help and usage errors stay readable.
    const bool own_console = termole::app::console_is_exclusive();

    auto parsed = termole::app::parse_cli(argc, argv);
    if (const auto action = termole::app::handle_cli_action(parsed)) return *action;

    const termole::app::CliOptions& options = parsed.options;
    if (options.mode == termole::app::LaunchMode::Stdio &&
        (!termole::app::standard_stream_is_redirected(STD_INPUT_HANDLE) ||
         !termole::app::standard_stream_is_redirected(STD_OUTPUT_HANDLE))) {
        report_error(L"--stdio requires stdin and stdout to be piped or redirected");
        return termole::app::kExitUsage;
    }

    if (own_console) (void)termole::app::hide_own_console();

    const bool wait_for_exit = options.mode != termole::app::LaunchMode::Normal;
    const HANDLE process = start_gui_role(argc, argv, wait_for_exit);
    if (!process) return termole::app::kExitProgramStartFailed;

    if (own_console) termole::app::release_console();

    if (!wait_for_exit) {
        ::CloseHandle(process);
        return 0;
    }

    ::WaitForSingleObject(process, INFINITE);
    DWORD exit_code = static_cast<DWORD>(termole::app::kExitProgramStartFailed);
    ::GetExitCodeProcess(process, &exit_code);
    ::CloseHandle(process);
    return static_cast<int>(exit_code);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc > 1 && std::wstring_view(argv[1]) == L"--gui") {
        return termole::app::run_gui(argc - 1, argv + 1);
    }
    return run_launcher(argc, argv);
}
