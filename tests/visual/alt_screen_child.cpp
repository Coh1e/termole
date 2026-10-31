// Deterministic alt-screen target for the pixel harness (tests/visual).
//
// Writes a known grid into the alternate screen and stays alive so the View
// holds stable content while the harness captures it. The parent terminates
// the session after capture; this program never restores the main screen on
// its own, which keeps the captured frame deterministic.

#include <windows.h>

#include <string>
#include <string_view>

namespace {

bool write_stdout(std::string_view text)
{
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!::WriteFile(output, text.data() + offset,
                         static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

} // namespace

int wmain(int, wchar_t**)
{
    ::SetConsoleOutputCP(CP_UTF8);

    std::string screen = "\x1b[?1049h\x1b[2J\x1b[H";
    screen += "ALT-SCREEN PIXEL PROBE\r\n";
    screen += "0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ\r\n";
    for (int row = 1; row <= 12; ++row) {
        screen += "line " + std::to_string(row) +
            ": the quick brown fox jumps over 13 lazy dogs\r\n";
    }
    screen += "\x1b[16;1H> probe-input";
    if (!write_stdout(screen)) return 3;

    // Long enough for any reasonable capture; the harness terminates the
    // session instead of waiting for this process to finish.
    ::Sleep(60000);
    return 0;
}
