#include <termole/termole.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
    return std::filesystem::path(std::wstring(buffer.data(), length)).replace_filename(
        L"cli-report-helper.exe");
}

void pump_messages()
{
    MSG message{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

bool wait_for_text(termole::Session& session, std::string_view marker, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        pump_messages();
        const auto text = session.screen_text();
        if (text && text->find(marker) != std::string::npos) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

bool wait_for_stopped(termole::Session& session, int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (session.running()) {
        pump_messages();
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

size_t line_count(const std::string& text)
{
    if (text.empty()) return 0;
    return static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
}

std::vector<termole::Event> collect_events(termole::Session& session)
{
    std::vector<termole::Event> events;
    for (;;) {
        auto event = session.poll_event();
        if (!event || !event->has_value()) break;
        events.push_back(std::move(**event));
    }
    return events;
}

bool has_event(const std::vector<termole::Event>& events, termole::EventType type)
{
    return std::any_of(events.begin(), events.end(),
                       [type](const termole::Event& event) { return event.type == type; });
}

termole::Event* find_event(std::vector<termole::Event>& events, termole::EventType type)
{
    const auto found = std::find_if(events.begin(), events.end(),
                                    [type](const termole::Event& event) { return event.type == type; });
    return found == events.end() ? nullptr : &*found;
}

HWND create_parent()
{
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = ::DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"TermoleT03T05Parent";
    ::RegisterClassW(&window_class);
    const HWND parent = ::CreateWindowExW(WS_EX_TOOLWINDOW, L"TermoleT03T05Parent", L"test",
                                          WS_OVERLAPPEDWINDOW, -30000, -30000, 800, 500, nullptr,
                                          nullptr, ::GetModuleHandleW(nullptr), nullptr);
    ::ShowWindow(parent, SW_SHOWNOACTIVATE);
    ::UpdateWindow(parent);
    return parent;
}

bool set_clipboard(const std::wstring& text)
{
    if (!::OpenClipboard(nullptr)) return false;
    ::EmptyClipboard();
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!memory) {
        ::CloseClipboard();
        return false;
    }
    auto* data = static_cast<wchar_t*>(::GlobalLock(memory));
    if (!data) {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }
    std::copy(text.begin(), text.end(), data);
    data[text.size()] = L'\0';
    ::GlobalUnlock(memory);
    ::SetClipboardData(CF_UNICODETEXT, memory);
    ::CloseClipboard();
    return true;
}

std::wstring get_clipboard()
{
    if (!::OpenClipboard(nullptr)) return {};
    std::wstring text;
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const auto* characters = static_cast<const wchar_t*>(::GlobalLock(data));
        if (characters) text.assign(characters);
        ::GlobalUnlock(data);
    }
    ::CloseClipboard();
    return text;
}

std::string file_contents(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

} // namespace

int wmain()
{
    const std::filesystem::path helper = helper_path();
    const auto report = std::filesystem::temp_directory_path() /
        ("termole-t03-" + std::to_string(::GetCurrentProcessId()) + ".txt");

    // T03: exit status and final output.
    termole::Session::Options exit_options;
    exit_options.program = helper.wstring();
    exit_options.arguments = { L"--exit-code", L"7" };
    exit_options.environment = { L"TERMOLE_PROBE_REPORT=" + report.wstring() };
    auto exit_session = termole::Session::create(exit_options);
    check(exit_session.has_value(), "exit-status session created");
    if (exit_session) {
        check(wait_for_stopped(*exit_session, 5000), "exit-status session stops");
        const auto code = exit_session->exit_code();
        check(code && *code == 7, "exit code is reported after the child stops");
        const auto screen = exit_session->screen_text();
        check(screen && screen->find("T02-final-marker") != std::string::npos,
              "final output is present when the exit code becomes visible");
    }

    // T05: title, bell and exited events.
    termole::Session::Options event_options;
    event_options.program = helper.wstring();
    event_options.arguments = { L"--events" };
    auto event_session = termole::Session::create(event_options);
    check(event_session.has_value(), "event session created");
    if (event_session) {
        check(wait_for_stopped(*event_session, 5000), "event session stops");
        auto events = collect_events(*event_session);
        check(has_event(events, termole::EventType::TitleChanged), "title event is queued");
        check(has_event(events, termole::EventType::Bell), "bell event is queued");
        auto* exited = find_event(events, termole::EventType::Exited);
        check(exited != nullptr && exited->exit_code == 7, "exit event carries the exit code");
        auto* title = find_event(events, termole::EventType::TitleChanged);
        check(title != nullptr && title->text == "Event Title", "title event carries the title text");
        const auto screen = event_session->screen_text();
        check(screen && screen->find("event-tail") != std::string::npos,
              "tail output is drained before the exit event is consumed");
        if (failures != 0) {
            std::cerr << "events: " << events.size() << '\n';
            for (const auto& event : events)
                std::cerr << "  type=" << static_cast<int>(event.type) << " code=" << event.exit_code
                          << " text=" << event.text << '\n';
            std::cerr << "screen:\n" << (screen ? *screen : std::string("<none>")) << '\n';
        }
    }

    // T05: queue bounds under a bell/title flood; exit event is preserved.
    termole::Session::Options flood_options;
    flood_options.program = helper.wstring();
    flood_options.arguments = { L"--flood", L"400" };
    auto flood_session = termole::Session::create(flood_options);
    check(flood_session.has_value(), "flood session created");
    if (flood_session) {
        check(wait_for_stopped(*flood_session, 5000), "flood session stops");
        auto events = collect_events(*flood_session);
        check(events.size() <= 256, "event queue is bounded");
        check(has_event(events, termole::EventType::Exited), "exit event survives event flooding");
    }

    // T04: bounded history and reattach.
    termole::Session::Options history_options;
    history_options.program = helper.wstring();
    history_options.arguments = { L"--lines", L"1100" };
    history_options.columns = 80;
    history_options.rows = 10;
    auto history_session = termole::Session::create(history_options);
    check(history_session.has_value(), "history session created");
    if (history_session) {
        check(wait_for_stopped(*history_session, 10000), "history session stops");
        const auto history = history_session->scrollback_text();
        check(history.has_value(), "scrollback query works");
        if (history) {
            check(line_count(*history) == 1000, "scrollback is capped at 1000 rows");
            check(history->find("line-0000") == std::string::npos, "oldest history rows are dropped");
            check(history->find("line-1089") != std::string::npos,
                  "newest history rows remain available");
        }
        const auto screen = history_session->screen_text();
        check(screen && screen->find("line-1099") != std::string::npos,
              "current screen keeps the newest rows");
    }

    termole::Session::Options alt_options;
    alt_options.program = helper.wstring();
    alt_options.arguments = { L"--altscreen" };
    auto alt_session = termole::Session::create(alt_options);
    check(alt_session.has_value(), "alternate-screen session created");
    if (alt_session) {
        check(wait_for_text(*alt_session, "ALT-SCREEN", 5000),
              "alternate screen receives output");
        const auto alt_screen = alt_session->screen_text();
        check(alt_screen && alt_screen->find("ALT-SCREEN") != std::string::npos &&
                  alt_screen->find("MAIN-SCREEN") == std::string::npos,
              "alternate screen hides the primary screen");
        check(wait_for_stopped(*alt_session, 5000), "alternate-screen session stops");
        const auto restored = alt_session->screen_text();
        check(restored && restored->find("MAIN-SCREEN") != std::string::npos &&
                  restored->find("RESTORED") != std::string::npos &&
                  restored->find("ALT-SCREEN") == std::string::npos,
              "leaving alternate screen restores the primary screen");
    }

    termole::Session::Options unicode_options;
    unicode_options.program = helper.wstring();
    unicode_options.arguments = { L"--unicode" };
    auto unicode_session = termole::Session::create(unicode_options);
    check(unicode_session.has_value(), "Unicode session created");
    if (unicode_session) {
        check(wait_for_stopped(*unicode_session, 5000), "Unicode session stops");
        const auto screen = unicode_session->screen_text();
        check(screen.has_value(), "Unicode screen query works");
        if (screen) {
            check(screen->find("\xE4\xB8\xAD" "A") != std::string::npos,
                  "wide CJK occupies two cells");
            check(screen->find("e\xCC\x81" "X") != std::string::npos,
                  "combining mark stays with its base cell");
            check(screen->find("\xF0\x9F\x90\xAD" "B") != std::string::npos,
                  "emoji occupies two cells");
            check(screen->find(
                      "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
                      "\xE2\x80\x8D\xF0\x9F\x91\xA6") != std::string::npos,
                  "ZWJ emoji sequence remains a grapheme cluster");
        }
    }

    const HWND parent = create_parent();
    check(parent != nullptr, "test parent window created");

    termole::Session::Options scroll_options;
    scroll_options.program = helper.wstring();
    scroll_options.arguments = { L"--lines", L"50" };
    scroll_options.columns = 80;
    scroll_options.rows = 10;
    auto scroll_session = termole::Session::create(scroll_options);
    check(scroll_session.has_value(), "scroll session created");
    if (scroll_session && parent) {
        check(wait_for_stopped(*scroll_session, 10000), "scroll session stops");
        auto scroll_view = termole::View::create(*scroll_session, parent);
        check(scroll_view.has_value(), "scroll view created");
        if (scroll_view) {
            const auto bottom = scroll_view->viewport_text();
            check(bottom && bottom->find("line-0049") != std::string::npos,
                  "viewport starts at the live screen");
            scroll_view->scroll(1000);
            const auto top = scroll_view->viewport_text();
            check(top && top->find("line-0000") != std::string::npos,
                  "scrolling exposes the oldest history");
            check(scroll_view->set_selection(0, 0, 0, 8).has_value(), "selection set");
            const auto selected = scroll_view->selection_text();
            check(selected && *selected == "line-0000", "selection text is exact");
            check(set_clipboard(L"old"), "test clipboard initialized");
            check(scroll_view->copy_selection().has_value(), "selection copied");
            check(get_clipboard() == L"line-0000", "system clipboard receives selection");
        }
    }

    const auto paste_file = std::filesystem::temp_directory_path() /
        ("termole-paste-" + std::to_string(::GetCurrentProcessId()) + ".txt");
    std::error_code paste_error;
    std::filesystem::remove(paste_file, paste_error);
    termole::Session::Options paste_options;
    paste_options.program = L"cmd.exe";
    paste_options.arguments = { L"/D", L"/Q" };
    auto paste_session = termole::Session::create(paste_options);
    check(paste_session.has_value(), "paste session created");
    if (paste_session && parent) {
        auto paste_view = termole::View::create(*paste_session, parent);
        check(paste_view.has_value(), "paste view created");
        if (paste_view) {
            check(set_clipboard(L"echo PASTE-OK> \"" + paste_file.wstring() + L"\""),
                  "paste clipboard initialized");
            check(paste_view->paste().has_value(), "host paste succeeds");
            (void)paste_session->send_key(termole::Key::Enter);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!std::filesystem::exists(paste_file) &&
                   std::chrono::steady_clock::now() < deadline) {
                pump_messages();
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            check(std::filesystem::exists(paste_file), "pasted command reaches cmd.exe");
            if (std::filesystem::exists(paste_file)) {
                check(file_contents(paste_file).find("PASTE-OK") != std::string::npos,
                      "pasted command produces expected output");
            }
        }
        (void)paste_session->terminate();
    }
    std::filesystem::remove(paste_file, paste_error);

    termole::Session::Options detach_options;
    detach_options.program = helper.wstring();
    detach_options.arguments = { L"--delayed-lines" };
    auto detach_session = termole::Session::create(detach_options);
    check(detach_session.has_value(), "detach session created");
    if (detach_session) {
        termole::View::Options appearance;
        appearance.font_family = L"Consolas";
        appearance.font_size_dip = 20;
        appearance.theme = TERMOLE_THEME_DARK;
        auto first_view = termole::View::create(*detach_session, parent, appearance);
        check(first_view.has_value(), "first view created with explicit appearance");
        if (first_view) {
            termole::View::Options light = appearance;
            light.font_size_dip = 18;
            light.theme = TERMOLE_THEME_LIGHT;
            check(first_view->set_appearance(light).has_value(),
                  "view appearance can change without recreation");
            termole::View::Options invalid = appearance;
            invalid.font_size_dip = 7;
            check(!first_view->set_appearance(invalid).has_value(),
                  "invalid font size is rejected");
        }
        check(wait_for_text(*detach_session, "before-detach", 5000),
              "session produces output while a view is attached");
        if (first_view) first_view->reset();
        check(wait_for_stopped(*detach_session, 5000),
              "session continues without a view and stops normally");

        auto second_view = termole::View::create(*detach_session, parent);
        check(second_view.has_value(), "view can be reattached to the same session");
        const auto screen = detach_session->screen_text();
        check(screen && screen->find("after-detach") != std::string::npos,
              "reattached view sees output produced without a view");
    }

    if (parent) ::DestroyWindow(parent);
    std::error_code error;
    std::filesystem::remove(report, error);

    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "PASS: T03/T04/T05 session lifetime, exit and events\n";
    return 0;
}
