#include <windows.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::wstring quote_cmd(const std::wstring& argument)
{
    std::wstring result = L"\"";
    for (const wchar_t character : argument) {
        if (character == L'"') result += L"\\\"";
        else result.push_back(character);
    }
    result += L"\"";
    return result;
}

std::wstring find_fzf()
{
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = ::SearchPathW(nullptr, L"fzf.exe", nullptr,
                                       static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length > 0 && length < buffer.size()) return std::wstring(buffer.data(), length);
    return {};
}

bool read_all(HANDLE handle, std::string& data)
{
    std::array<char, 65536> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            const DWORD error = ::GetLastError();
            if (error == ERROR_BROKEN_PIPE) return true;
            return false;
        }
        if (read == 0) return true;
        data.append(buffer.data(), read);
    }
}

bool write_all(HANDLE handle, std::string_view data)
{
    size_t offset = 0;
    while (offset < data.size()) {
        DWORD written = 0;
        if (!::WriteFile(handle, data.data() + offset, static_cast<DWORD>(data.size() - offset),
                         &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

void diagnostic(std::wstring_view message)
{
    const std::wstring text = L"termole-picker: " + std::wstring(message) + L"\n";
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (length <= 0) return;
    std::string bytes(static_cast<size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(),
                          length, nullptr, nullptr);
    (void)write_all(::GetStdHandle(STD_ERROR_HANDLE), bytes);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring fzf = find_fzf();
    std::vector<std::wstring> arguments;
    for (int index = 1; index < argc; ++index) {
        if (std::wstring_view(argv[index]) == L"--fzf") {
            if (index + 1 >= argc) {
                diagnostic(L"--fzf requires a path");
                return 2;
            }
            fzf = argv[++index];
        } else {
            arguments.emplace_back(argv[index]);
        }
    }
    if (fzf.empty()) {
        diagnostic(L"fzf.exe was not found on PATH");
        return 127;
    }
    const DWORD attributes = ::GetFileAttributesW(fzf.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        diagnostic(L"fzf executable was not found");
        return 127;
    }

    std::string candidates;
    if (!read_all(::GetStdHandle(STD_INPUT_HANDLE), candidates)) {
        diagnostic(L"could not read candidate input");
        return 1;
    }

    wchar_t temp_path[MAX_PATH]{};
    if (::GetTempPathW(MAX_PATH, temp_path) == 0) {
        diagnostic(L"could not locate the temporary directory");
        return 1;
    }
    wchar_t temp_file[MAX_PATH]{};
    if (::GetTempFileNameW(temp_path, L"trm", 0, temp_file) == 0) {
        diagnostic(L"could not create a temporary file");
        return 1;
    }
    const std::filesystem::path work(temp_file);
    const auto candidate_path = work.wstring() + L".in";
    const auto result_path = work.wstring() + L".out";
    const auto script_path = work.wstring() + L".cmd";
    std::filesystem::remove(work);

    {
        std::ofstream file(candidate_path, std::ios::binary);
        file.write(candidates.data(), static_cast<std::streamsize>(candidates.size()));
    }

    std::wstring command = L"@echo off\r\n";
    command += quote_cmd(fzf);
    for (const auto& argument : arguments) command += L" " + quote_cmd(argument);
    command += L" < " + quote_cmd(candidate_path) + L" > " + quote_cmd(result_path) + L"\r\n";
    {
        const int bytes = ::WideCharToMultiByte(CP_ACP, 0, command.data(),
                                               static_cast<int>(command.size()), nullptr, 0,
                                               nullptr, nullptr);
        std::string ansi(static_cast<size_t>(std::max(0, bytes)), '\0');
        if (bytes > 0) {
            ::WideCharToMultiByte(CP_ACP, 0, command.data(), static_cast<int>(command.size()),
                                  ansi.data(), bytes, nullptr, nullptr);
        }
        std::ofstream file(script_path, std::ios::binary);
        file.write(ansi.data(), static_cast<std::streamsize>(ansi.size()));
    }

    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    std::array<wchar_t, 32768> comspec{};
    const DWORD comspec_length = ::GetEnvironmentVariableW(L"ComSpec", comspec.data(),
                                                           static_cast<DWORD>(comspec.size()));
    std::wstring application = comspec_length > 0 && comspec_length < comspec.size()
        ? std::wstring(comspec.data(), comspec_length)
        : std::wstring(L"C:\\Windows\\System32\\cmd.exe");
    std::wstring command_line = L"\"" + application + L"\" /D /Q /C " + quote_cmd(script_path);
    if (!::CreateProcessW(application.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                          CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process)) {
        const DWORD error = ::GetLastError();
        diagnostic(L"could not start fzf (cmd error " + std::to_wstring(error) + L")");
        std::filesystem::remove(candidate_path);
        std::filesystem::remove(script_path);
        return 1;
    }
    ::CloseHandle(process.hThread);
    ::WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    ::GetExitCodeProcess(process.hProcess, &exit_code);
    ::CloseHandle(process.hProcess);

    std::string result;
    {
        std::ifstream file(result_path, std::ios::binary);
        result.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::error_code error;
    std::filesystem::remove(candidate_path, error);
    std::filesystem::remove(result_path, error);
    std::filesystem::remove(script_path, error);

    if (!write_all(::GetStdHandle(STD_OUTPUT_HANDLE), result)) {
        diagnostic(L"could not write the selection");
        return 1;
    }
    return static_cast<int>(exit_code);
}
