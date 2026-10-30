#include <windows.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int spawn_long_running_child(const wchar_t* pid_file)
{
    std::array<wchar_t, 32768> module{};
    const DWORD length =
        ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (length == 0 || length >= module.size()) return 2;

    std::wstring command = L"\"" + std::wstring(module.data(), length) + L"\" --sleep";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                          &startup, &process)) {
        return 2;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, pid_file, L"w") == 0 && file) {
        std::fprintf(file, "%lu", process.dwProcessId);
        std::fclose(file);
    }

    ::CloseHandle(process.hThread);
    ::WaitForSingleObject(process.hProcess, 60000);
    ::CloseHandle(process.hProcess);
    return 0;
}

} // namespace

// A real console program used through ConPTY by the public-interface test. It
// emits a small VT script and exits, so the test observes the actual parser and
// screen-state path rather than an in-process mock terminal.
int wmain(int argc, wchar_t** argv)
{
    if (argc >= 2 && std::wstring(argv[1]) == L"--sleep") {
        ::Sleep(60000);
        return 0;
    }
    if (argc >= 3 && std::wstring(argv[1]) == L"--spawn-child") {
        return spawn_long_running_child(argv[2]);
    }

    const char* script =
        "\x1b]0;Helper Title\x07"
        "\x1b[2J\x1b[H"
        "TOP\r\n"
        "\x1b[2;3HX"
        "\x1b[1;1HR"
        "\x1b[?25l";

    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    ::WriteFile(output, script, static_cast<DWORD>(std::strlen(script)), &written, nullptr);
    ::Sleep(400);
    return 0;
}
