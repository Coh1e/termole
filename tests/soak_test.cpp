#include <termole/termole.hpp>

#include <windows.h>
#include <psapi.h>

#include <chrono>
#include <algorithm>
#include <iostream>
#include <string>
#include <thread>

namespace {
size_t working_set()
{
    PROCESS_MEMORY_COUNTERS counters{};
    if (!::GetProcessMemoryInfo(::GetCurrentProcess(), &counters, sizeof(counters))) return 0;
    return counters.WorkingSetSize;
}

bool wait_stopped(termole::Session& session, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (session.running()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return true;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    const std::wstring helper = argv[1];
    const size_t before = working_set();
    size_t peak = before;
    for (int run = 0; run < 3; ++run) {
        termole::Session::Options options;
        options.program = helper;
        options.arguments = { L"--lines", L"200000" };
        options.columns = 80;
        options.rows = 24;
        auto session = termole::Session::create(options);
        if (!session) {
            std::cerr << "session creation failed: " << session.error().message() << '\n';
            return 1;
        }
        if (!wait_stopped(*session, 60000)) {
            std::cerr << "long output did not finish\n";
            (void)session->terminate();
            return 1;
        }
        const auto history = session->scrollback_text();
        if (!history || std::count(history->begin(), history->end(), '\n') > 1000) {
            std::cerr << "history exceeded its bound\n";
            (void)session->terminate();
            return 1;
        }
        (void)session->terminate();
        peak = std::max(peak, working_set());
    }
    const size_t growth = peak > before ? peak - before : 0;
    std::cout << "before=" << before << " peak=" << peak << " growth=" << growth << '\n';
    if (growth > 64ull * 1024 * 1024) {
        std::cerr << "working-set growth exceeded 64 MiB\n";
        return 1;
    }
    std::cout << "PASS: 3x200k lines with bounded history and working set\n";
    return 0;
}
