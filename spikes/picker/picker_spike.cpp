// ConPTY setup adapted from Microsoft Terminal's MIT-licensed EchoCon sample.
// Copyright (c) Microsoft Corporation. See THIRD_PARTY_NOTICES.md.
// This is a feasibility harness, not the public Termole implementation.
#include <windows.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
namespace fs = std::filesystem;
using unique_pseudoconsole = wil::unique_any<HPCON, decltype(&ClosePseudoConsole), ClosePseudoConsole>;

// Windows CRT argument quoting, following Microsoft's documented parsing rules.
std::wstring quote(const std::wstring& argument)
{
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const auto ch : argument) {
        if (ch == L'\\') { ++backslashes; continue; }
        result.append(backslashes * (ch == L'"' ? 2 : 1), L'\\');
        backslashes = 0;
        if (ch == L'"') result += L'\\';
        result += ch;
    }
    result.append(backslashes * 2, L'\\');
    return result + L'"';
}

void write_all(HANDLE handle, const std::string& bytes)
{
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        THROW_IF_WIN32_BOOL_FALSE(WriteFile(handle, bytes.data() + offset,
            static_cast<DWORD>(bytes.size() - offset), &written, nullptr));
        THROW_HR_IF(E_FAIL, written == 0);
        offset += written;
    }
}

std::string read_file(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input), {} };
}

// Already attached to ConPTY; preserve independent data handles while fzf
// opens CONIN$/CONOUT$ for interaction. stderr remains the console as required
// by fzf's upstream Windows renderer. This helper does not interpret data.
int child(int argc, wchar_t** argv)
{
    if (argc < 6) return 125;
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    wil::unique_handle console(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    THROW_LAST_ERROR_IF(!console);
    const auto data_in = reinterpret_cast<HANDLE>(std::stoull(argv[3]));
    const auto data_out = reinterpret_cast<HANDLE>(std::stoull(argv[4]));
    STARTUPINFOW startup{ sizeof(startup) };
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = data_in;
    startup.hStdOutput = data_out;
    startup.hStdError = console.get();
    std::wstring command = quote(argv[2]);
    for (int i = 5; i < argc; ++i) command += L" " + quote(argv[i]);
    PROCESS_INFORMATION process{};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(argv[2], command.data(), nullptr, nullptr,
        TRUE, 0, nullptr, nullptr, &startup, &process));
    wil::unique_handle thread(process.hThread);
    wil::unique_handle running(process.hProcess);
    // Do not retain data endpoints after forwarding them to fzf.
    CloseHandle(data_in);
    CloseHandle(data_out);
    const auto waited = WaitForSingleObject(running.get(), 20000);
    if (waited != WAIT_OBJECT_0) { TerminateProcess(running.get(), 125); return 125; }
    DWORD code = 0;
    THROW_IF_WIN32_BOOL_FALSE(GetExitCodeProcess(running.get(), &code));
    return static_cast<int>(code);
}

struct Case {
    std::string name;
    std::string input;
    std::vector<std::wstring> options;
    std::string ready_text;
    std::string keys;
    std::string expected;
    DWORD expected_code = 0;
    std::string alternate_expected;
    bool close_window = false;
};

bool run_case(const fs::path& self, const fs::path& fzf, const fs::path& evidence, const Case& test)
{
    SECURITY_ATTRIBUTES security{ sizeof(security), nullptr, TRUE };
    wil::unique_handle pty_in, input_writer, output_reader, pty_out;
    THROW_IF_WIN32_BOOL_FALSE(CreatePipe(pty_in.put(), input_writer.put(), nullptr, 0));
    THROW_IF_WIN32_BOOL_FALSE(CreatePipe(output_reader.put(), pty_out.put(), nullptr, 0));
    unique_pseudoconsole pseudo;
    THROW_IF_FAILED(CreatePseudoConsole({ 80, 24 }, pty_in.get(), pty_out.get(), 0, pseudo.put()));
    pty_in.reset();
    pty_out.reset();

    wil::unique_handle candidate_reader, candidate_writer, result_reader, result_writer;
    THROW_IF_WIN32_BOOL_FALSE(CreatePipe(candidate_reader.put(), candidate_writer.put(), &security, 0));
    THROW_IF_WIN32_BOOL_FALSE(CreatePipe(result_reader.put(), result_writer.put(), &security, 0));
    THROW_IF_WIN32_BOOL_FALSE(SetHandleInformation(candidate_writer.get(), HANDLE_FLAG_INHERIT, 0));
    THROW_IF_WIN32_BOOL_FALSE(SetHandleInformation(result_reader.get(), HANDLE_FLAG_INHERIT, 0));

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 2, 0, &size);
    std::vector<std::byte> storage(size);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    THROW_IF_WIN32_BOOL_FALSE(InitializeProcThreadAttributeList(attributes, 2, 0, &size));
    auto cleanup = wil::scope_exit([&] { DeleteProcThreadAttributeList(attributes); });
    THROW_IF_WIN32_BOOL_FALSE(UpdateProcThreadAttribute(attributes, 0,
        PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, pseudo.get(), sizeof(HPCON), nullptr, nullptr));
    std::array<HANDLE, 2> inherited{ candidate_reader.get(), result_writer.get() };
    THROW_IF_WIN32_BOOL_FALSE(UpdateProcThreadAttribute(attributes, 0,
        PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited.data(), sizeof(inherited), nullptr, nullptr));

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    auto command = quote(self.wstring()) + L" --child " + quote(fzf.wstring()) + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(candidate_reader.get())) + L" " +
        std::to_wstring(reinterpret_cast<uintptr_t>(result_writer.get()));
    for (const auto& argument : test.options) command += L" " + quote(argument);

    wil::unique_handle job(CreateJobObjectW(nullptr, nullptr));
    THROW_LAST_ERROR_IF(!job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    THROW_IF_WIN32_BOOL_FALSE(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)));
    PROCESS_INFORMATION process{};
    THROW_IF_WIN32_BOOL_FALSE(CreateProcessW(self.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, nullptr, nullptr, &startup.StartupInfo, &process));
    wil::unique_handle thread(process.hThread), running(process.hProcess);
    if (!AssignProcessToJobObject(job.get(), running.get())) {
        TerminateProcess(running.get(), 125);
        THROW_LAST_ERROR();
    }
    THROW_LAST_ERROR_IF(ResumeThread(thread.get()) == static_cast<DWORD>(-1));
    candidate_reader.reset();
    result_writer.reset();

    std::mutex screen_mutex;
    std::string screen, result;
    DWORD screen_error = 0, result_error = 0;
    // Keep draining during ClosePseudoConsole to avoid synchronous close deadlocks.
    auto reader = [](HANDLE handle, std::string& into, std::mutex* mutex, DWORD& error) {
        std::array<char, 4096> buffer{};
        DWORD count = 0;
        while (ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr) && count) {
            if (mutex) { std::lock_guard guard(*mutex); into.append(buffer.data(), count); }
            else into.append(buffer.data(), count);
        }
        error = GetLastError();
    };
    std::thread screen_thread(reader, output_reader.get(), std::ref(screen), &screen_mutex, std::ref(screen_error));
    std::thread result_thread(reader, result_reader.get(), std::ref(result), nullptr, std::ref(result_error));
    // Every assertion/Win32 failure below still closes the process tree and joins.
    auto finish = wil::scope_exit([&] {
        job.reset();
        pseudo.reset();
        input_writer.reset();
        screen_thread.join();
        result_thread.join();
    });
    write_all(candidate_writer.get(), test.input);
    candidate_writer.reset();

    bool ready = false;
    auto deadline = std::chrono::steady_clock::now() + 8s;
    while (std::chrono::steady_clock::now() < deadline && WaitForSingleObject(running.get(), 0) == WAIT_TIMEOUT) {
        { std::lock_guard guard(screen_mutex); ready = screen.find(test.ready_text) != std::string::npos; }
        if (ready) break;
        std::this_thread::sleep_for(10ms);
    }
    if (ready) {
        if (test.close_window) THROW_IF_WIN32_BOOL_FALSE(TerminateJobObject(job.get(), 130));
        else write_all(input_writer.get(), test.keys);
    }
    const auto waited = WaitForSingleObject(running.get(), 10000);
    DWORD code = 125;
    if (waited == WAIT_OBJECT_0) THROW_IF_WIN32_BOOL_FALSE(GetExitCodeProcess(running.get(), &code));
    job.reset();
    pseudo.reset();
    input_writer.reset();
    screen_thread.join();
    result_thread.join();
    finish.release();

    std::ofstream(evidence / (test.name + ".vt"), std::ios::binary).write(screen.data(), screen.size());
    std::ofstream(evidence / (test.name + ".result"), std::ios::binary).write(result.data(), result.size());
    // fzf's select-all order is its own contract; accept either complete order,
    // without sorting or modifying the bytes actually captured from stdout.
    const bool bytes_match = result == test.expected ||
        (!test.alternate_expected.empty() && result == test.alternate_expected);
    const bool passed = ready && waited == WAIT_OBJECT_0 && code == test.expected_code && bytes_match;
    std::cout << test.name << ": " << (passed ? "PASS" : "FAIL") << " ready=" << ready
        << " exit=" << code << " result_bytes=" << result.size() << " display_bytes=" << screen.size()
        << " screen_error=" << screen_error << " result_error=" << result_error << '\n';
    return passed;
}

int wmain(int argc, wchar_t** argv)
try {
    if (argc > 1 && std::wstring(argv[1]) == L"--child") return child(argc, argv);
    if (argc != 3) { std::cerr << "usage: picker_spike fzf.exe evidence_directory\n"; return 125; }
    std::array<wchar_t, 32768> executable{};
    THROW_LAST_ERROR_IF(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())) == 0);
    const fs::path evidence = fs::absolute(argv[2]);
    fs::create_directories(evidence);
    // No ambient user configuration may change the harness protocol.
    SetEnvironmentVariableW(L"FZF_DEFAULT_OPTS", L"");
    SetEnvironmentVariableW(L"FZF_DEFAULT_OPTS_FILE", L"");
    std::vector<Case> cases{
        { "single", "alpha\nbeta\ngamma\n", { L"--no-mouse", L"--query=beta" }, "beta", "\r", "beta\n" },
        { "cancel", "alpha\nbeta\n", { L"--no-mouse" }, "alpha", "\x1b", "", 130 },
        { "multi", "alpha\nbeta\n", { L"--no-mouse", L"--multi", L"--bind=load:select-all" }, "alpha", "\r", "alpha\nbeta\n", 0, "beta\nalpha\n" },
        { "nul", std::string("space name\0line\nbreak\0", 22), { L"--no-mouse", L"--read0", L"--print0", L"--query=space" }, "space", "\r", std::string("space name\0", 11) },
        { "nul_newline", std::string("space name\0line\nbreak\0", 22), { L"--no-mouse", L"--read0", L"--print0", L"--query=break" }, "break", "\r", std::string("line\nbreak\0", 11) },
        { "utf8", "alpha\n\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x90\xad\n", { L"--no-mouse", L"--query=\u4e2d\u6587" }, "\xe4\xb8\xad", "\r", "\xe4\xb8\xad\xe6\x96\x87 \xf0\x9f\x90\xad\n" },
        { "close", "alpha\nbeta\n", { L"--no-mouse" }, "alpha", "", "", 130, "", true },
    };
    std::string large;
    for (int i = 0; i < 20000; ++i) large += "candidate-" + std::to_string(i) + "\n";
    large += "unique-stream-target\n";
    cases.push_back({ "large_pipe", std::move(large), { L"--no-mouse", L"--query=unique-stream-target" },
        "unique-stream-target", "\r", "unique-stream-target\n" });
    bool passed = true;
    for (const auto& test : cases) passed = run_case(executable.data(), fs::absolute(argv[1]), evidence, test) && passed;
    return passed ? 0 : 1;
} catch (const wil::ResultException& error) {
    std::cerr << "Win32 failure: " << error.what() << '\n'; return 125;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 125;
}
