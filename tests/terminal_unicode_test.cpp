#include "core/terminal.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const char* what)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}
} // namespace

int main()
{
    {
        termole::Terminal terminal(10, 2);
        terminal.input(U'\u4E2D');
        terminal.input(U'A');
        const auto snapshot = terminal.snapshot();
        check(snapshot.cells[0].character == U'\u4E2D' && snapshot.cells[0].width == 2,
              "CJK is wide");
        check(snapshot.cells[1].continuation, "wide CJK has a continuation cell");
        check(snapshot.cells[2].character == U'A', "text after wide CJK starts at column 2");
    }
    {
        termole::Terminal terminal(10, 2);
        terminal.input(U'e');
        terminal.input(U'\u0301');
        terminal.input(U'X');
        const auto snapshot = terminal.snapshot();
        check(snapshot.cells[0].character == U'e' && snapshot.cells[0].combining.size() == 1 &&
                  snapshot.cells[0].combining[0] == U'\u0301',
              "combining mark joins the base cell");
        check(snapshot.cells[1].character == U'X', "combining mark consumes no cell");
    }
    {
        termole::Terminal terminal(10, 2);
        terminal.input(U'\U0001F42D');
        terminal.input(U'B');
        const auto snapshot = terminal.snapshot();
        check(snapshot.cells[0].character == U'\U0001F42D' && snapshot.cells[0].width == 2,
              "emoji is wide");
        check(snapshot.cells[1].continuation, "emoji has a continuation cell");
        check(snapshot.cells[2].character == U'B', "text after emoji starts at column 2");
    }
    {
        termole::Terminal terminal(10, 2);
        terminal.input(U'\U0001F468');
        terminal.input(U'\u200D');
        terminal.input(U'\U0001F469');
        terminal.input(U'\u200D');
        terminal.input(U'\U0001F467');
        terminal.input(U'\u200D');
        terminal.input(U'\U0001F466');
        const auto snapshot = terminal.snapshot();
        check(snapshot.cells[0].character == U'\U0001F468' &&
                  snapshot.cells[0].combining.size() == 6,
              "ZWJ family is one grapheme cluster");
        check(snapshot.cells[0].width == 2, "ZWJ family remains two cells wide");
        const std::string expected =
            "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7"
            "\xE2\x80\x8D\xF0\x9F\x91\xA6";
        check(terminal.text() == expected, "ZWJ family round-trips through screen text");
    }
    {
        termole::Terminal terminal(10, 2);
        const char32_t sequence[] = U"\x1b[?1000h\x1b[?1006h\x1b[?2004h\x1b[?1h";
        for (const char32_t codepoint : sequence) terminal.input(codepoint);
        check(terminal.mouse_mode() == 1, "click mouse mode is parsed");
        check(terminal.sgr_mouse(), "SGR mouse mode is parsed");
        check(terminal.bracketed_paste(), "bracketed paste mode is parsed");
        check(terminal.application_cursor(), "application cursor mode is parsed");
        const char32_t disable[] = U"\x1b[?1000l\x1b[?1006l\x1b[?2004l\x1b[?1l";
        for (const char32_t codepoint : disable) terminal.input(codepoint);
        check(terminal.mouse_mode() == 0 && !terminal.sgr_mouse() &&
                  !terminal.bracketed_paste() && !terminal.application_cursor(),
              "terminal modes can be disabled");
    }
    if (failures != 0) return 1;
    std::cout << "PASS: Unicode width and grapheme cells\n";
    return 0;
}
