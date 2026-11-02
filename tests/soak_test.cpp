// T20 resource soak: sustained output, title/bell floods and repeated
// Session host lifetimes must stay bounded. Every measurement is taken from
// the public wrapper, so the test observes the same contract a host uses.

#include <termole/termole.hpp>

#include "test_process.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

int failures = 0;

using termole_test::handle_count;

void check(bool condition, std::string_view what)
{
    if (condition) {
        std::cout << "PASS: " << what << '\n';
    } else {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

size_t line_count(const std::string& text)
{
    if (text.empty()) return 0;
    return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

termole::Event* find_event(std::vector<termole::Event>& events, termole::EventType type)
{
    const auto found = std::find_if(events.begin(), events.end(),
                                    [type](const termole::Event& event) {
                                        return event.type == type;
                                    });
    return found == events.end() ? nullptr : &*found;
}

// running() becomes false before the reader queues the terminal event, so a
// host must keep polling for a short bounded window instead of treating the
// first empty poll as "no exit event".
std::vector<termole::Event> collect_events_until_exited(termole::Session& session,
                                                        int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::vector<termole::Event> events;
    for (;;) {
        for (;;) {
            auto event = session.poll_event();
            if (!event || !event->has_value()) break;
            events.push_back(std::move(**event));
        }
        if (find_event(events, termole::EventType::Exited) != nullptr) return events;
        if (std::chrono::steady_clock::now() >= deadline) return events;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

termole::Session::Options options_for(const std::wstring& helper,
                                      std::vector<std::wstring> arguments)
{
    termole::Session::Options options;
    options.program = helper;
    options.arguments = std::move(arguments);
    options.columns = 80;
    options.rows = 24;
    return options;
}

void long_output_without_view(const std::wstring& helper)
{
    const size_t before = working_set();
    size_t peak = before;
    bool bounded = true;
    int completed = 0;
    for (int run = 0; run < 3; ++run) {
        auto session = termole::Session::create(
            options_for(helper, { L"--lines", L"200000" }));
        if (!session) {
            check(false, "long output: session created");
            return;
        }
        if (!wait_stopped(*session, 60000)) {
            check(false, "long output: session stops");
            (void)session->terminate();
            return;
        }
        const auto history = session->scrollback_text();
        if (!history || line_count(*history) > 1000) bounded = false;
        (void)session->terminate();
        ++completed;
        peak = std::max(peak, working_set());
    }
    const size_t growth = peak > before ? peak - before : 0;
    std::cout << "T20 long-output: runs=" << completed << " ws_before=" << before
              << " peak=" << peak << " growth=" << growth << '\n';
    check(completed == 3, "long output: every no-View run completed");
    check(bounded, "long output: history stays within 1000 lines");
    check(growth <= 64ull * 1024 * 1024, "long output: working-set growth stays under 64 MiB");
}

void alternating_title_bell_flood(const std::wstring& helper)
{
    constexpr int kFloodEvents = 20000;
    const std::string final_title_utf8 = "Flood " + std::to_string(kFloodEvents - 1);
    const size_t before = working_set();
    auto session = termole::Session::create(
        options_for(helper, { L"--flood", std::to_wstring(kFloodEvents) }));
    check(session.has_value(), "flood: session created");
    if (!session) return;

    const auto start = std::chrono::steady_clock::now();
    check(wait_stopped(*session, 30000), "flood: session stops");
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    auto events = collect_events_until_exited(*session, 2000);
    const size_t after = working_set();
    const size_t growth = after > before ? after - before : 0;

    const auto title = session->title();
    termole::Event* final_title_event = nullptr;
    for (auto& event : events) {
        if (event.type == termole::EventType::TitleChanged) final_title_event = &event;
    }
    const termole::Event* exited = find_event(events, termole::EventType::Exited);

    std::cout << "T20 flood: events=" << events.size() << " elapsed_ms=" << elapsed
              << " ws_before=" << before << " ws_after=" << after << " growth=" << growth
              << '\n';
    check(events.size() <= 256, "flood: event queue stays within 256 entries");
    check(exited != nullptr && exited->exit_code == 0,
          "flood: exit event survives the title/bell flood");
    check(title && *title == final_title_utf8, "flood: last terminal title is retained");
    check(final_title_event != nullptr && final_title_event->text == final_title_utf8,
          "flood: newest title event survives queue eviction");
    check(growth <= 32ull * 1024 * 1024, "flood: working-set growth stays under 32 MiB");
}

void repeated_session_lifetimes(const std::wstring& helper)
{
    constexpr int kCycles = 24;
    const auto create = [&]() {
        return termole::Session::create(options_for(helper, { L"--lines", L"200" }));
    };

    // One warm-up cycle keeps process-wide lazy allocation out of the baseline.
    {
        auto warm = create();
        check(warm.has_value(), "multi-session: warm-up session created");
        if (!warm) return;
        if (!wait_stopped(*warm, 10000)) {
            check(false, "multi-session: warm-up session stops");
            (void)warm->terminate();
            return;
        }
        warm->reset();
    }

    const DWORD handles_before = handle_count();
    const size_t ws_before = working_set();
    int completed = 0;
    bool bounded = true;
    for (int index = 0; index < kCycles; ++index) {
        auto session = create();
        if (!session) {
            check(false, "multi-session: session created");
            break;
        }
        if (!wait_stopped(*session, 10000)) {
            check(false, "multi-session: session stops");
            session->reset();
            break;
        }
        const auto history = session->scrollback_text();
        const auto code = session->exit_code();
        if (!history || line_count(*history) > 1000 || !code || *code != 0) bounded = false;
        session->reset();
        ++completed;
    }

    const DWORD handles_after = handle_count();
    const size_t ws_after = working_set();
    const size_t growth = ws_after > ws_before ? ws_after - ws_before : 0;
    std::cout << "T20 multi-session: cycles=" << completed << " handles=" << handles_before
              << "->" << handles_after << " ws_before=" << ws_before << " ws_after=" << ws_after
              << " growth=" << growth << '\n';
    check(completed == kCycles, "multi-session: every create/run/destroy cycle completed");
    check(bounded, "multi-session: every history and exit code stayed bounded");
    check(handles_after <= handles_before + 4, "multi-session: host handle count stays bounded");
    check(growth <= 32ull * 1024 * 1024, "multi-session: working-set growth stays under 32 MiB");
}

void concurrent_session_burst(const std::wstring& helper)
{
    constexpr int kSessions = 8;
    const DWORD handles_before = handle_count();
    const size_t ws_before = working_set();

    std::vector<termole::Session> sessions;
    sessions.reserve(kSessions);
    int created = 0;
    for (int index = 0; index < kSessions; ++index) {
        auto session = termole::Session::create(
            options_for(helper, { L"--delayed-lines" }));
        if (!session) {
            check(false, "concurrent: session created");
            break;
        }
        sessions.push_back(std::move(*session));
        ++created;
    }

    // Destroy half of the overlapping sessions while the rest are still in
    // their delayed-output window. The survivors then finish normally.
    const int terminated = created / 2;
    for (int index = 0; index < terminated; ++index) sessions[static_cast<size_t>(index)].reset();

    bool bounded = true;
    for (size_t index = static_cast<size_t>(terminated); index < sessions.size(); ++index) {
        if (!wait_stopped(sessions[index], 10000)) {
            check(false, "concurrent: surviving session stops");
            sessions[index].reset();
            bounded = false;
            continue;
        }
        const auto history = sessions[index].scrollback_text();
        if (!history || line_count(*history) > 1000) bounded = false;
    }
    sessions.clear();

    const DWORD handles_after = handle_count();
    const size_t ws_after = working_set();
    const size_t growth = ws_after > ws_before ? ws_after - ws_before : 0;
    std::cout << "T20 concurrent: sessions=" << created << " terminated=" << terminated
              << " handles=" << handles_before << "->" << handles_after
              << " ws_before=" << ws_before << " ws_after=" << ws_after << " growth=" << growth
              << '\n';
    check(created == kSessions, "concurrent: all sessions were created");
    check(bounded, "concurrent: surviving histories stay bounded");
    check(handles_after <= handles_before + 4, "concurrent: host handle count stays bounded");
    check(growth <= 32ull * 1024 * 1024, "concurrent: working-set growth stays under 32 MiB");
}

void repeated_start_failures(const std::wstring& helper)
{
    const DWORD handles_before = handle_count();

    // A missing program is rejected before any pipe or pseudo console exists.
    const auto missing = std::filesystem::temp_directory_path() /
        (L"termole-t20-missing-" + std::to_wstring(::GetCurrentProcessId()) + L".exe");
    std::error_code missing_error;
    std::filesystem::remove(missing, missing_error);
    const bool missing_absent = !std::filesystem::exists(missing, missing_error);
    check(!missing_error && missing_absent, "start-failure: missing-program path is absent");
    int missing_failures = 0;
    for (int index = 0; index < 20; ++index) {
        auto session = termole::Session::create(options_for(missing.wstring(), {}));
        if (!session) ++missing_failures;
    }
    const DWORD handles_after_missing = handle_count();

    // An existing image held open exclusively reaches the loader preflight, so
    // the public create path must reject it without leaving Windows-internal
    // handles behind. Before the preflight this loop grew the host handle table
    // on every attempt.
    const auto locked = std::filesystem::temp_directory_path() /
        (L"termole-t20-locked-" + std::to_wstring(::GetCurrentProcessId()) + L".exe");
    std::error_code ignored;
    std::filesystem::remove(locked, ignored);
    std::filesystem::copy_file(helper, locked, std::filesystem::copy_options::overwrite_existing,
                               ignored);
    check(!ignored, "start-failure: helper copy succeeded");
    int locked_failures = 0;
    DWORD handles_after_warmup = handles_before;
    if (!ignored) {
        const HANDLE exclusive = ::CreateFileW(locked.c_str(), GENERIC_READ, 0, nullptr,
                                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(exclusive != INVALID_HANDLE_VALUE, "start-failure: helper opened exclusively");
        if (exclusive != INVALID_HANDLE_VALUE) {
            for (int index = 0; index < 20; ++index) {
                auto session = termole::Session::create(options_for(locked.wstring(), {}));
                if (!session) ++locked_failures;
                // The first failures initialize process-wide Windows telemetry;
                // judge the trend after that lazy state exists.
                if (index == 4) handles_after_warmup = handle_count();
            }
            ::CloseHandle(exclusive);
        }
    }
    std::filesystem::remove(locked, ignored);

    const DWORD handles_after = handle_count();
    std::cout << "T20 start-failure: missing=" << missing_failures << " locked=" << locked_failures
              << " handles=" << handles_before << "->" << handles_after_missing << "->"
              << handles_after_warmup << "->" << handles_after << '\n';
    check(missing_failures == 20, "start-failure: every missing-program creation fails");
    check(locked_failures == 20, "start-failure: every locked-program creation fails");
    check(handles_after <= handles_after_warmup + 2,
          "start-failure: failed creations do not leak handles after warm-up");
}

void app_execution_alias_starts()
{
    std::array<wchar_t, 32768> local_app_data{};
    const DWORD length = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                                                    static_cast<DWORD>(local_app_data.size()));
    if (length == 0 || length >= local_app_data.size()) {
        std::cout << "SKIP: LOCALAPPDATA is unavailable; App Execution Alias test skipped\n";
        return;
    }
    const auto alias = std::filesystem::path(std::wstring(local_app_data.data(), length)) /
        L"Microsoft" / L"WindowsApps" / L"winget.exe";
    const DWORD attributes = ::GetFileAttributesW(alias.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        std::cout << "SKIP: winget.exe App Execution Alias is unavailable\n";
        return;
    }

    // The fallback only applies when CreateFileW cannot read-open the alias.
    // If this host allows the read, the ordinary preflight path is exercised
    // instead and there is nothing new to prove here.
    const HANDLE probe = ::CreateFileW(alias.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(probe);
        std::cout << "SKIP: winget.exe alias is readable; fallback not exercised\n";
        return;
    }
    // The fallback accepts any refused read-open on an alias, not one specific
    // error code, so report the code instead of asserting it.
    std::cout << "alias: read-open error=" << ::GetLastError() << '\n';

    auto session = termole::Session::create(options_for(alias.wstring(), { L"--version" }));
    check(session.has_value(), "alias: App Execution Alias passes the program-image preflight");
    if (!session) return;
    check(wait_stopped(*session, 15000), "alias: winget --version stops");
    const auto code = session->exit_code();
    check(code && *code == 0, "alias: winget --version exits 0");
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) return 2;
    const std::wstring helper = argv[1];

    long_output_without_view(helper);
    alternating_title_bell_flood(helper);
    repeated_session_lifetimes(helper);
    concurrent_session_burst(helper);
    repeated_start_failures(helper);
    app_execution_alias_starts();

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T20 sustained output, title/bell flood and Session lifetimes stay bounded\n";
    return 0;
}
