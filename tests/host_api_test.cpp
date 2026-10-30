#include <termole/termole.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, std::string_view what)
{
    if (condition) {
        std::cout << "PASS: " << what << '\n';
    } else {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

std::filesystem::path helper_path()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    std::filesystem::path path(std::wstring(buffer.data(), length));
    path.replace_filename(L"termole-test-helper.exe");
    return path;
}

bool regular_file(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

bool real_pwsh_available()
{
    for (const wchar_t* variable : { L"ProgramFiles", L"ProgramW6432" }) {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length =
            ::GetEnvironmentVariableW(variable, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) continue;
        if (regular_file(std::filesystem::path(std::wstring(buffer.data(), length)) /
                            L"PowerShell/7/pwsh.exe")) {
            return true;
        }
    }
    std::array<wchar_t, 32768> local{};
    const DWORD local_length =
        ::GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
    if (local_length > 0 && local_length < local.size() &&
        regular_file(std::filesystem::path(std::wstring(local.data(), local_length)) /
                        L"Microsoft/WindowsApps/pwsh.exe")) {
        return true;
    }

    std::array<wchar_t, 32768> found{};
    const DWORD found_length = ::SearchPathW(nullptr, L"pwsh.exe", nullptr,
                                              static_cast<DWORD>(found.size()), found.data(), nullptr);
    return found_length > 0 && found_length < found.size();
}

void pump_messages()
{
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms,
                   std::string* last = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string current;
    for (;;) {
        pump_messages();
        const auto text = session.screen_text();
        if (text) {
            current = *text;
            if (current.find(marker) != std::string::npos) {
                if (last) *last = current;
                return true;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (last) *last = current;
    return false;
}

bool wait_for_resize(termole::Session& session, std::pair<uint16_t, uint16_t> before, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        const auto size = session.size();
        if (size && *size != before) return true;
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool verify_console_size(termole::Session& session, uint16_t columns, uint16_t rows, int timeout_ms)
{
    const auto command =
        session.write("Write-Output ('console-' + [Console]::WindowWidth + 'x' + "
                      "[Console]::WindowHeight)\r");
    if (!command) return false;
    const std::string expected =
        "console-" + std::to_string(columns) + "x" + std::to_string(rows);
    return wait_for_text(session, expected, timeout_ms);
}

void type_through_view(HWND view, std::wstring_view text)
{
    for (const wchar_t character : text) {
        ::SendMessageW(view, WM_CHAR, static_cast<WPARAM>(character), 0);
    }
}

} // namespace

int wmain()
{
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    check(termole_abi_version() == TERMOLE_ABI_VERSION, "ABI version matches the header");

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ::DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"TermoleT01TestParent";
    check(::RegisterClassW(&window_class) != 0, "test parent class registered");

    const HWND parent = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"TermoleT01TestParent", L"t01",
                                          WS_OVERLAPPEDWINDOW, -30000, -30000, 1200, 800, nullptr,
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
    check(parent != nullptr, "test parent window created");
    if (!parent) return 1;
    ::ShowWindow(parent, SW_SHOWNOACTIVATE);
    ::UpdateWindow(parent);

    termole::Session::Options options;
    options.arguments = { L"-NoProfile" };
    auto session_result = termole::Session::create(options);
    check(session_result.has_value(), "default PowerShell session created");
    if (!session_result) {
        std::cerr << session_result.error().message() << '\n';
        return 1;
    }
    auto session = std::move(*session_result);

    auto view_result = termole::View::create(session, parent);
    check(view_result.has_value(), "view created through the public wrapper");
    if (!view_result) {
        std::cerr << view_result.error().message() << '\n';
        return 1;
    }
    auto view = std::move(*view_result);
    ::MoveWindow(view.hwnd(), 0, 0, 1000, 700, TRUE);
    ::UpdateWindow(view.hwnd());
    check(::GetParent(view.hwnd()) == parent, "view is a child of the host window");

    auto second_view = termole::View::create(session, parent);
    check(!second_view.has_value(), "a second view on the same session is rejected");
    check(!second_view && second_view.error().status() == TERMOLE_STATUS_INVALID_STATE,
          "the second-view rejection uses the structured invalid-state error");

    // Raw session input plus the key encoder. The marker is assembled from
    // separate string literals so the echoed command line cannot satisfy it.
    check(session.write("Write-Output ('termole-' + 'host-ok')").has_value(),
          "command input accepted");
    check(session.send_key(termole::Key::Enter).has_value(), "Enter key encoded");
    std::string screen;
    check(wait_for_text(session, "termole-host-ok", 20000, &screen),
          "PowerShell output reached the screen through the public session API");
    if (failures != 0) std::cerr << "screen after first command:\n" << screen << '\n';

    // The same behavior through View keyboard input: Printable characters,
    // Backspace and Enter must reach the real shell. The expected marker is
    // only formed by command output, never by the echoed input.
    type_through_view(view.hwnd(), L"Write-Output ('termole-' + 'VX");
    check(wait_for_text(session, "VX", 5000), "view keyboard input is echoed by PowerShell");
    ::SendMessageW(view.hwnd(), WM_CHAR, L'\b', 0);
    type_through_view(view.hwnd(), L"C' + '')\r");
    check(wait_for_text(session, "termole-VC", 10000, &screen),
          "view Backspace and Enter edit and execute a real command");
    if (failures != 0) std::cerr << "screen after backspace command:\n" << screen << '\n';

    const auto before = session.size();
    check(before.has_value(), "session size query works");
    if (before) {
        ::SetWindowPos(parent, nullptr, -30000, -30000, 1500, 1000, SWP_NOZORDER | SWP_NOACTIVATE);
        ::MoveWindow(view.hwnd(), 0, 0, 1400, 900, TRUE);
        check(wait_for_resize(session, *before, 3000),
              "resizing the host child window updates the ConPTY grid");
        const auto after = session.size();
        check(after.has_value(), "session size query works after the window resize");
        if (after) {
            check(verify_console_size(session, after->first, after->second, 5000),
                  "ConPTY reports the console size implied by the window resize");
        }
    }

    check(session.resize(42, 13).has_value(), "direct session resize accepted");
    const auto resized = session.size();
    check(resized && resized->first == 42 && resized->second == 13,
          "session size reflects the direct resize");
    check(verify_console_size(session, 42, 13, 5000),
          "ConPTY reports the direct 42x13 resize");

    // A controlled real console process drives the VT path (clear, cursor
    // addressing, SGR, title) without a mock terminal.
    termole::Session::Options helper_options;
    helper_options.program = helper_path().wstring();
    auto helper_result = termole::Session::create(helper_options);
    check(helper_result.has_value(), "controlled VT helper session created");
    if (helper_result) {
        auto helper = std::move(*helper_result);
        std::string helper_screen;
        check(wait_for_text(helper, "ROP", 5000, &helper_screen),
              "VT helper output reached the terminal");
        check(helper_screen == "ROP\n  X", "cursor addressing and erase produced the expected grid");
        const auto title = helper.title();
        check(title && *title == "Helper Title", "OSC title is exposed through the public query");
        (void)helper.terminate();
    }

    // Destroying the Session handle must terminate the process tree even when
    // the caller never calls terminate() explicitly.
    const auto pid_file = std::filesystem::temp_directory_path() /
        ("termole-t01-" + std::to_string(::GetCurrentProcessId()) + ".pid");
    std::error_code ignored;
    std::filesystem::remove(pid_file, ignored);

    termole::Session::Options tree_options;
    tree_options.program = helper_path().wstring();
    tree_options.arguments = { L"--spawn-child", pid_file.wstring() };
    auto tree_result = termole::Session::create(tree_options);
    check(tree_result.has_value(), "process-tree helper session created");
    if (tree_result) {
        auto tree = std::move(*tree_result);
        DWORD child_pid = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (child_pid == 0 && std::chrono::steady_clock::now() < deadline) {
            std::ifstream input(pid_file);
            unsigned long value = 0;
            if (input >> value) {
                child_pid = static_cast<DWORD>(value);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        check(child_pid != 0, "helper reported its descendant process id");
        if (child_pid != 0) {
            tree.reset();
            const HANDLE child = ::OpenProcess(SYNCHRONIZE, FALSE, child_pid);
            if (child) {
                const DWORD wait = ::WaitForSingleObject(child, 5000);
                check(wait == WAIT_OBJECT_0, "destroying the Session kills the descendant process tree");
                ::CloseHandle(child);
            } else {
                check(::GetLastError() == ERROR_INVALID_PARAMETER,
                      "descendant process is already gone after Session destruction");
            }
        }
        std::filesystem::remove(pid_file, ignored);
    }

    // Default-shell selection must prefer a pwsh.exe found on PATH over the
    // Windows PowerShell fallback. On machines with a real PowerShell 7 the
    // probe is skipped; here the PATH entry is a real console helper renamed to
    // pwsh.exe, so the selected executable is still a real ConPTY client.
    if (real_pwsh_available()) {
        std::cout << "SKIP: real PowerShell 7 present; PATH priority probe not applicable\n";
    } else {
        const auto fake_dir = std::filesystem::temp_directory_path() /
            ("termole-t01-pwsh-" + std::to_string(::GetCurrentProcessId()));
        std::error_code error;
        std::filesystem::remove_all(fake_dir, error);
        std::filesystem::create_directories(fake_dir, error);
        std::filesystem::copy_file(helper_path(), fake_dir / L"pwsh.exe",
                                   std::filesystem::copy_options::overwrite_existing, error);
        check(!error, "fake PowerShell 7 executable prepared");

        std::array<wchar_t, 32768> old_path_buffer{};
        const DWORD old_length =
            ::GetEnvironmentVariableW(L"PATH", old_path_buffer.data(),
                                      static_cast<DWORD>(old_path_buffer.size()));
        const std::wstring old_path(old_path_buffer.data(), old_length);
        const std::wstring new_path = fake_dir.wstring() + L";" + old_path;
        check(::SetEnvironmentVariableW(L"PATH", new_path.c_str()) != FALSE,
              "PATH updated for the default-shell probe");

        auto fake_pwsh = termole::Session::create();
        bool selected = false;
        if (fake_pwsh) selected = wait_for_text(*fake_pwsh, "ROP", 5000);
        check(selected, "no-config launch prefers pwsh.exe from PATH over Windows PowerShell");
        if (fake_pwsh) (void)fake_pwsh->terminate();

        ::SetEnvironmentVariableW(L"PATH", old_length == 0 ? nullptr : old_path.c_str());
        std::filesystem::remove_all(fake_dir, error);
    }

    termole_session_options bad_options = TERMOLE_SESSION_OPTIONS_INIT;
    bad_options.size = 4;
    termole_session* raw_session = nullptr;
    check(termole_session_create(&bad_options, &raw_session) == TERMOLE_STATUS_ABI_MISMATCH,
          "options size mismatch is rejected");
    check(std::strlen(termole_last_error()) > 0, "ABI error leaves a diagnostic");
    check(termole_session_create(nullptr, &raw_session) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "null options are rejected");

    termole_session_options bad_range = TERMOLE_SESSION_OPTIONS_INIT;
    bad_range.columns = 2000;
    check(termole_session_create(&bad_range, &raw_session) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "out-of-range grid size is rejected with a structured status");

    const auto cleanup_start = std::chrono::steady_clock::now();
    view.reset();
    check(session.terminate().has_value(), "explicit session termination reports success");
    const auto closed_write = session.write("x");
    check(!closed_write && closed_write.error().status() == TERMOLE_STATUS_INVALID_STATE,
          "writing after termination returns INVALID_STATE");
    session.reset();
    const auto cleanup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - cleanup_start)
                                .count();
    check(cleanup_ms < 5000, "view and session teardown completes promptly");

    ::DestroyWindow(parent);
    ::UnregisterClassW(window_class.lpszClassName, window_class.hInstance);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T01 public host interface\n";
    return 0;
}
