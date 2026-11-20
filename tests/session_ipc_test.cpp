#include "ipc/session_ipc.h"
#include "ipc/session_protocol.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {
int serve(std::string_view name)
{
    termole::ipc::ServerSessionOptions options;
#ifdef _WIN32
    options.program = L"cmd.exe";
    options.arguments = { L"/d", L"/q" };
#else
    options.arguments = { "/bin/sh" };
#endif
    return termole::ipc::run_server(name, options);
}

struct ServerProcess {
#ifdef _WIN32
    HANDLE process = nullptr;

    bool start(const std::string& name)
    {
        wchar_t image[32768];
        const DWORD length = ::GetModuleFileNameW(nullptr, image, 32768);
        if (!length || length >= 32768) return false;
        std::wstring command = L"\"" + std::wstring(image, length) + L"\" --serve ";
        command.append(name.begin(), name.end());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION child{};
        if (!::CreateProcessW(image, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                              nullptr, nullptr, &startup, &child)) return false;
        process = child.hProcess;
        ::CloseHandle(child.hThread);
        return true;
    }

    bool done()
    {
        DWORD code = 1;
        return ::WaitForSingleObject(process, 4000) == WAIT_OBJECT_0 &&
            ::GetExitCodeProcess(process, &code) && code == 0;
    }

    ~ServerProcess()
    {
        if (!process) return;
        if (::WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
            ::TerminateProcess(process, 1);
            ::WaitForSingleObject(process, 4000);
        }
        ::CloseHandle(process);
    }
#else
    pid_t pid = -1;

    bool start(const std::string& name)
    {
        pid = ::fork();
        if (pid == 0) _exit(serve(name));
        return pid > 0;
    }

    bool done()
    {
        int status = 0;
        if (::waitpid(pid, &status, 0) != pid) return false;
        pid = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    ~ServerProcess()
    {
        if (pid <= 0) return;
        (void)::kill(pid, SIGTERM);
        (void)::waitpid(pid, nullptr, 0);
    }
#endif
};

bool sees(termole::ipc::RemoteSession& session, std::string_view marker)
{
    if (auto frame = session.frame_after(0)) {
        std::string text;
        for (const auto& cell : frame->cells) {
            const uint32_t first = cell.codepoint_count ? cell.codepoints[0] : 0;
            text.push_back(first < 128 && first != 0 ? static_cast<char>(first) : ' ');
        }
        const size_t index = text.find(marker);
        if (index == std::string::npos) return false;
        termole::FrameSelection selection;
        selection.begin(index + marker.size() - 1);
        selection.focus = index;
        return selection.text(*frame) == marker;
    }
    return false;
}
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string_view(argv[1]) == "--serve") return serve(argv[2]);
    uint8_t header[TERMOLE_IPC_HEADER_SIZE];
    termole_ipc_write_header(header, { 17, TERMOLE_IPC_FRAME, TERMOLE_IPC_VERSION });
    termole_ipc_header decoded{};
    if (!termole_ipc_read_header(header, &decoded) || decoded.size != 17 ||
        decoded.type != TERMOLE_IPC_FRAME || decoded.version != TERMOLE_IPC_VERSION) return 1;
    termole_ipc_store_u32(header, TERMOLE_IPC_MAX_PAYLOAD + 1);
    if (termole_ipc_read_header(header, &decoded)) return 1;

#ifdef _WIN32
    const auto id = ::GetCurrentProcessId();
    constexpr std::string_view print = "set IPC=KEPT\recho IPC-%IPC%\r";
    constexpr std::string_view quit = "exit /b 7\r";
#else
    const auto id = ::getpid();
    constexpr std::string_view print = "printf 'IPC-%s\\n' KEPT\r";
    constexpr std::string_view quit = "exit 7\r";
#endif
    const std::string name = "probe-" + std::to_string(id);
    ServerProcess server;
    if (!server.start(name)) return 1;

    std::string error;
    std::unique_ptr<termole::ipc::RemoteSession> first;
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!first && std::chrono::steady_clock::now() < deadline) {
        first = termole::ipc::RemoteSession::connect(name, true, error);
        if (!first) std::this_thread::sleep_for(20ms);
    }
    if (!first || !first->resize(100, 30, error) || !first->write(print, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    first.reset();

    auto raw = termole::ipc::invalid_handle;
    const auto reject_deadline = std::chrono::steady_clock::now() + 2s;
    while (raw == termole::ipc::invalid_handle &&
           std::chrono::steady_clock::now() < reject_deadline) {
        raw = termole::ipc::connect_local(name, error);
        if (raw == termole::ipc::invalid_handle) std::this_thread::sleep_for(20ms);
    }
    if (raw == termole::ipc::invalid_handle) return 1;
    termole_ipc_write_header(header, { 0, TERMOLE_IPC_ATTACH, TERMOLE_IPC_VERSION + 1 });
    const bool rejected = termole::ipc::send_bytes(raw, header) &&
        termole::ipc::read_bytes(raw, header) && termole_ipc_read_header(header, &decoded) &&
        decoded.type == TERMOLE_IPC_ERROR && decoded.version == TERMOLE_IPC_VERSION;
    termole::ipc::close_local(raw);
    if (!rejected) return 1;

    std::unique_ptr<termole::ipc::RemoteSession> second;
    const auto reconnect_deadline = std::chrono::steady_clock::now() + 3s;
    while (!second && std::chrono::steady_clock::now() < reconnect_deadline) {
        second = termole::ipc::RemoteSession::connect(name, false, error);
        if (!second) std::this_thread::sleep_for(20ms);
    }
    if (!second) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    const auto output_deadline = std::chrono::steady_clock::now() + 3s;
    while (!sees(*second, "IPC-KEPT") && std::chrono::steady_clock::now() < output_deadline)
        std::this_thread::sleep_for(20ms);
    if (!sees(*second, "IPC-KEPT") || !second->write(quit, error)) return 1;

    const auto exit_deadline = std::chrono::steady_clock::now() + 3s;
    while (second->running() && std::chrono::steady_clock::now() < exit_deadline)
        std::this_thread::sleep_for(20ms);
    if (second->wait() != 7) return 1;
    second.reset();

    return server.done() ? 0 : 1;
}
