#include <vtbackend/MockTerm.h>
#include <vtbackend/Screen.h>
#include <iostream>
#include <stdexcept>
int main() {
    using namespace vtbackend;
    auto term = MockTerm{ColumnCount{20}, LineCount{4}};
    term.writeToScreen("hello\x1b[31m red\x1b[0m\r\n");
    auto first = term.terminal.currentScreen().lineTextAt(LineOffset{0});
    if (first != "hello red") throw std::runtime_error("VT screen mismatch: " + first);
    term.writeToScreen("\x1b[?1049hALT");
    if (!term.terminal.isAlternateScreen()) throw std::runtime_error("alternate screen not entered");
    term.writeToScreen("\x1b[?1049l");
    if (!term.terminal.isPrimaryScreen()) throw std::runtime_error("primary screen not restored");
    term.sendCharSequence("abc");
    if (term.replyData() != "abc") throw std::runtime_error("keyboard encoding mismatch");
    term.writeToScreen("\r\n\xe4\xb8\xad\xe6\x96\x87");
    auto cjk = term.terminal.currentScreen().lineTextAt(LineOffset{2});
    if (cjk != "\xe4\xb8\xad\xe6\x96\x87") throw std::runtime_error("CJK text mismatch: " + cjk);
    if (term.terminal.currentScreen().cellWidthAt(CellLocation{LineOffset{2}, ColumnOffset{0}}) != 2) throw std::runtime_error("CJK width mismatch");
    std::cout << "PASS: VT text, alternate screen, keyboard encoding, CJK text and width\n";
}
