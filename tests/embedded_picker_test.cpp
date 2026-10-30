#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Run {
    DWORD exit_code = 1;
    std::string output;
};

Run run(const std::filesystem::path& app, const std::vector<std::wstring>& arguments,
        const std::string& candidates, bool close_stdout = false)
{    const auto root = std::filesystem::temp_directory_path() /
        ("termole-embedded-picker-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto input = root / "in.bin";
    const auto output = root / "out.bin";
    {
        std::ofstream file(input, std::ios::binary);
        file.write(candidates.data(), static_cast<std::streamsize>(candidates.size()));
    }
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE input_handle = ::CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, &security,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE output_handle = INVALID_HANDLE_VALUE;
    if (close_stdout) {
        HANDLE read_handle = INVALID_HANDLE_VALUE;
        ::CreatePipe(&read_handle, &output_handle, &security, 0);
        if (read_handle != INVALID_HANDLE_VALUE) ::CloseHandle(read_handle);
    } else {
        output_handle = ::CreateFileW(output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_handle;
    startup.hStdOutput = output_handle;
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    std::wstring command = L"\"" + app.wstring() + L"\" --picker";
    for (const auto& argument : arguments) command += L" --picker-arg \"" + argument + L"\"";
    PROCESS_INFORMATION process{};
    const bool started = ::CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                                          nullptr, nullptr, &startup, &process) != FALSE;
    ::CloseHandle(input_handle);
    ::CloseHandle(output_handle);
    Run result;
    if (!started) return result;
    ::WaitForSingleObject(process.hProcess, 30000);
    ::GetExitCodeProcess(process.hProcess, &result.exit_code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    if (!close_stdout) {
        std::ifstream file(output, std::ios::binary);
        result.output.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::filesystem::remove_all(root, error);
    return result;
}

Run run_slow_reader(const std::filesystem::path& app, const std::string& candidates)
{
    const auto root = std::filesystem::temp_directory_path() /
        ("termole-embedded-picker-slow-" + std::to_string(::GetCurrentProcessId()));
    std::error_code error;
    std::filesystem::create_directories(root, error);
    const auto input = root / "in.bin";
    {
        std::ofstream file(input, std::ios::binary);
        file.write(candidates.data(), static_cast<std::streamsize>(candidates.size()));
    }
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    HANDLE input_handle = ::CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, &security,
                                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE read_handle = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;
    ::CreatePipe(&read_handle, &write_handle, &security, 0);
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_handle;
    startup.hStdOutput = write_handle;
    startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
    std::wstring command = L"\"" + app.wstring() + L"\" --picker --picker-arg --filter --picker-arg item-";
    PROCESS_INFORMATION process{};
    const bool started = ::CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                                          nullptr, nullptr, &startup, &process) != FALSE;
    ::CloseHandle(input_handle);
    ::CloseHandle(write_handle);
    Run result;
    if (!started) {
        ::CloseHandle(read_handle);
        return result;
    }
    std::string output;
    std::thread reader([&] {
        std::array<char, 512> buffer{};
        for (;;) {
            DWORD read = 0;
            if (!::ReadFile(read_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                            nullptr) ||
                read == 0)
                break;
            output.append(buffer.data(), read);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    ::WaitForSingleObject(process.hProcess, 30000);
    ::GetExitCodeProcess(process.hProcess, &result.exit_code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    reader.join();
    ::CloseHandle(read_handle);
    result.output = std::move(output);
    std::filesystem::remove_all(root, error);
    return result;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    const std::filesystem::path app = argv[1];
    int failures = 0;

    const auto single = run(app, { L"--filter", L"banana" }, "apple\r\nbanana\r\ncherry\r\n");
    if (single.exit_code != 0 || single.output.find("banana") == std::string::npos ||
        single.output.find("apple") != std::string::npos) {
        std::cerr << "embedded single-select failed: exit=" << single.exit_code
                  << " out=" << single.output << '\n';
        ++failures;
    }

    std::string candidates = "alpha";
    candidates.push_back('\0');
    candidates += "banana";
    candidates.push_back('\0');
    candidates += "\xE4\xB8\xAD\xE6\x96\x87";
    candidates.push_back('\0');
    const auto nul = run(app, { L"--read0", L"--print0", L"--filter", L"\u4E2D\u6587" },
                         candidates);
    const std::string expected = "\xE4\xB8\xAD\xE6\x96\x87";
    if (nul.exit_code != 0 || nul.output.size() != expected.size() + 1 ||
        nul.output.compare(0, expected.size(), expected) != 0 || nul.output.back() != '\0') {
        std::cerr << "embedded NUL/Unicode failed: exit=" << nul.exit_code
                  << " bytes=" << nul.output.size() << '\n';
        ++failures;
    }

    const auto multi = run(app, { L"--multi", L"--filter", L"ana" },
                           "apple\r\nbanana\r\ncherry\r\n");
    if (multi.exit_code != 0 || multi.output.find("banana") == std::string::npos ||
        multi.output.find("cherry") != std::string::npos) {
        std::cerr << "embedded multi-select failed: exit=" << multi.exit_code
                  << " out=" << multi.output << '\n';
        ++failures;
    }

    std::string large;
    for (int index = 0; index < 12000; ++index) {
        large += "item-" + std::to_string(index) + "\r\n";
    }
    const auto large_result = run(app, { L"--filter", L"item-11999" }, large);
    if (large_result.exit_code != 0 ||
        large_result.output.find("item-11999") == std::string::npos) {
        std::cerr << "embedded large-input failed: exit=" << large_result.exit_code
                  << " bytes=" << large_result.output.size() << '\n';
        ++failures;
    }

    const auto multi_result = run(app, { L"--multi", L"--filter", L"item-1199" }, large);
    if (multi_result.exit_code != 0 ||
        multi_result.output.find("item-11990") == std::string::npos ||
        multi_result.output.find("item-11999") == std::string::npos) {
        std::cerr << "embedded multi-filter failed: exit=" << multi_result.exit_code
                  << " bytes=" << multi_result.output.size() << '\n';
        ++failures;
    }

    const auto closed = run(app, { L"--filter", L"banana" }, "apple\r\nbanana\r\n", true);
    if (closed.exit_code != 0) {
        std::cerr << "closed stdout did not complete cleanly: exit=" << closed.exit_code << '\n';
        ++failures;
    }

    const auto slow = run_slow_reader(app, large);
    if (slow.exit_code != 0 || slow.output.find("item-0\n") == std::string::npos ||
        slow.output.find("item-11999\n") == std::string::npos) {
        std::cerr << "slow-reader backpressure failed: exit=" << slow.exit_code
                  << " bytes=" << slow.output.size() << '\n';
        ++failures;
    }

    if (failures != 0) return 1;
    std::cout << "PASS: embedded picker single/multi, NUL/Unicode and large input\n";
    return 0;
}
