#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Run {
    DWORD exit_code = 1;
    std::string output;
    std::string error;
};

Run run_picker(const std::filesystem::path& picker, const std::filesystem::path& fzf,
               const std::vector<std::wstring>& arguments, const std::string& candidates)
{
    const auto root = std::filesystem::temp_directory_path() /
        ("termole-picker-test-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto input = root / "in.bin";
    const auto output = root / "out.bin";
    const auto errors = root / "err.bin";
    {
        std::ofstream file(input, std::ios::binary);
        file.write(candidates.data(), static_cast<std::streamsize>(candidates.size()));
    }

    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE input_handle = ::CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, &security,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE output_handle = ::CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE error_handle = ::CreateFileW(errors.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_handle;
    startup.hStdOutput = output_handle;
    startup.hStdError = error_handle;
    std::wstring command = L"\"" + picker.wstring() + L"\" --fzf \"" + fzf.wstring() + L"\"";
    for (const auto& argument : arguments) command += L" \"" + argument + L"\"";
    PROCESS_INFORMATION process{};
    const bool started = ::CreateProcessW(picker.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                                          nullptr, nullptr, &startup, &process) != FALSE;
    ::CloseHandle(input_handle);
    ::CloseHandle(output_handle);
    ::CloseHandle(error_handle);
    Run result;
    if (!started) return result;
    ::WaitForSingleObject(process.hProcess, 20000);
    ::GetExitCodeProcess(process.hProcess, &result.exit_code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    {
        std::ifstream file(output, std::ios::binary);
        result.output.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    {
        std::ifstream file(errors, std::ios::binary);
        result.error.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove_all(root, error);
    return result;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 3) return 2;
    const std::filesystem::path picker = argv[1];
    const std::filesystem::path fzf = argv[2];
    int failures = 0;

    const auto single = run_picker(picker, fzf, { L"--filter", L"banana" },
                                   "apple\r\nbanana\r\ncherry\r\n");
    if (single.exit_code != 0 || single.output.find("banana") == std::string::npos ||
        single.output.find("apple") != std::string::npos) {
        std::cerr << "single-select failed: exit=" << single.exit_code
                  << " out=" << single.output << " err=" << single.error << '\n';
        ++failures;
    }

    std::string nul_candidates = "alpha";
    nul_candidates.push_back('\0');
    nul_candidates += "banana";
    nul_candidates.push_back('\0');
    nul_candidates += "\xE4\xB8\xAD\xE6\x96\x87";
    nul_candidates.push_back('\0');
    const auto nul = run_picker(picker, fzf,
                                { L"--read0", L"--print0", L"--filter", L"\u4E2D\u6587" },
                                nul_candidates);
    const std::string expected = "\xE4\xB8\xAD\xE6\x96\x87";
    if (nul.exit_code != 0 || nul.output.size() != expected.size() + 1 ||
        nul.output.compare(0, expected.size(), expected) != 0 || nul.output.back() != '\0') {
        std::cerr << "NUL/Unicode transfer failed: exit=" << nul.exit_code
                  << " bytes=" << nul.output.size() << '\n';
        ++failures;
    }

    const auto missing = run_picker(picker, L"termole-no-such-fzf.exe", { L"--filter", L"x" }, "x\n");
    if (missing.exit_code != 127 || missing.error.find("fzf") == std::string::npos) {
        std::cerr << "missing fzf status failed: exit=" << missing.exit_code << '\n';
        ++failures;
    }

    if (failures != 0) return 1;
    std::cout << "PASS: picker single-select, NUL/Unicode and missing-tool status\n";
    return 0;
}
