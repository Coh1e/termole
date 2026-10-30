#include <termole/termole.hpp>

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int wmain()
{
    termole::Session::Options options;
    options.program = L"C:\\Windows\\System32\\cmd.exe";
    options.arguments = { L"/D", L"/Q" };
    options.columns = 80;
    options.rows = 10;
    auto session = termole::Session::create(options);
    if (!session) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    std::string line = "echo " + std::string(200, 'X');
    (void)session->write(line);
    (void)session->send_key(termole::Key::Enter);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const auto screen = session->screen_text();
    (void)session->terminate();
    if (!screen) return 1;
    size_t x_count = 0;
    size_t longest = 0;
    size_t line_count = 0;
    for (size_t start = 0; start <= screen->size();) {
        const size_t end = screen->find('\n', start);
        const size_t length = (end == std::string::npos ? screen->size() : end) - start;
        longest = std::max(longest, length);
        ++line_count;
        for (size_t i = start; i < start + length; ++i)
            if ((*screen)[i] == 'X') ++x_count;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::cout << "lines=" << line_count << " longest=" << longest << " X=" << x_count << '\n';
    std::cout << "screen:\n" << *screen << '\n';
    if (x_count < 200 || longest > 80) return 1;
    std::cout << "PASS: long line is grid-wrapped\n";
    return 0;
}
