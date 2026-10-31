// Controlled target for the T12 mouse/bracketed-paste acceptance (#13).
//
// This is a small VT input probe, not a test hook: Termole drives it through a
// real ConPTY. It enables xterm mouse and bracketed-paste modes, reads the raw
// VT input bytes the console delivers, and writes both a structured log (the
// exact bytes and decoded events) and a fixed-row status screen. The visual
// harness posts real View mouse/key messages and captures the window pixels.
//
// Commands are typed text of the form ~COMMAND~:
//   ~MODE=1002,1006,2004~   enable modes (1000/1002/1003, 1006, 2004)
//   ~MODE=1000,1006=0~      X10 encoding (1006 off)
//   ~MODE=OFF~              disable every mouse/paste mode
//   ~CURSOR=APP~            enable application cursor keys (?1)
//   ~CURSOR=NORMAL~         disable application cursor keys
//   ~HISTORY~               leave enough scrollback for local wheel tests
//
// Log lines (one per record, flushed immediately):
//   READY|vt_input=1
//   CHUNK|hex=<bytes read from the console>
//   EVENT|sgr|b=0|x=5|y=3|press=1
//   EVENT|x10|b=0|x=5|y=3|press=1
//   PASTE|bracket=1|hex=<payload bytes>
//   TEXT|hex=<typed bytes>
//   KEY|hex=<CSI/SS3 sequence consumed as a key>
//   CMD|MODE|modes=1002,1006,2004
//   CMD|HISTORY|rows=22|lines=30
//   CMD|HISTORY-READY
//   READ-FAILED|error=<code>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <string_view>

namespace {

constexpr int kRowTitle = 0;
constexpr int kRowModes = 1;
constexpr int kRowAlpha = 2;
constexpr int kRowBeta = 3;
constexpr int kRowGamma = 4;
constexpr int kRowMouse = 5;
constexpr int kRowPaste = 6;
constexpr int kRowText = 7;
constexpr int kRowStatus = 8;

HANDLE g_log = INVALID_HANDLE_VALUE;

bool write_all(HANDLE handle, std::string_view text)
{
    size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!::WriteFile(handle, text.data() + offset,
                         static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
            written == 0) {
            return false;
        }
        offset += written;
    }
    return true;
}

void log_line(std::string_view line)
{
    if (g_log == INVALID_HANDLE_VALUE) return;
    std::string text(line);
    text += "\r\n";
    write_all(g_log, text);
    ::FlushFileBuffers(g_log);
}

void display(std::string_view text)
{
    write_all(::GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void set_line(int row, std::string_view text)
{
    display("\x1b[" + std::to_string(row + 1) + ";1H\x1b[2K");
    display(text);
}

std::string visible_escape(std::string_view bytes)
{
    std::string out;
    for (const unsigned char value : bytes) {
        if (value >= 0x20 && value < 0x7f) {
            out.push_back(static_cast<char>(value));
        } else {
            char buffer[5]{};
            std::snprintf(buffer, sizeof(buffer), "\\x%02x", value);
            out += buffer;
        }
    }
    return out;
}

std::string hex_encode(std::string_view bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        out.push_back(digits[value >> 4]);
        out.push_back(digits[value & 0xf]);
    }
    return out;
}

std::string modes_text(const bool mode_1000, const bool mode_1002, const bool mode_1003,
                       const bool sgr, const bool bracketed)
{
    if (!mode_1000 && !mode_1002 && !mode_1003 && !sgr && !bracketed) return "OFF";
    std::string out;
    const auto append = [&](std::string_view value) {
        if (!out.empty()) out.push_back(',');
        out += value;
    };
    if (mode_1000) append("1000");
    if (mode_1002) append("1002");
    if (mode_1003) append("1003");
    if (sgr) append("1006");
    if (bracketed) append("2004");
    return out;
}

void show_mouse(const std::string& text)
{
    set_line(kRowMouse, "LAST-MOUSE=" + text);
}

void show_paste(const std::string& text)
{
    set_line(kRowPaste, "LAST-PASTE=" + text);
}

void show_text(const std::string& text)
{
    set_line(kRowText, "LAST-TEXT=" + visible_escape(text));
}

void show_status(std::string_view text)
{
    set_line(kRowStatus, "STATUS=" + std::string(text));
}

void show_modes(const std::string& modes)
{
    set_line(kRowModes, "MODES=" + modes);
}

void apply_mode_command(std::string_view value)
{
    bool mode_1000 = false;
    bool mode_1002 = false;
    bool mode_1003 = false;
    bool sgr = false;
    bool bracketed = false;
    if (value != "OFF") {
        size_t position = 0;
        while (position <= value.size()) {
            const size_t end = value.find(',', position);
            const std::string token{ value.substr(
                position, (end == std::string_view::npos ? value.size() : end) - position) };
            if (token == "1000") mode_1000 = true;
            if (token == "1002") mode_1002 = true;
            if (token == "1003") mode_1003 = true;
            if (token == "1006") sgr = true;
            if (token == "2004") bracketed = true;
            if (end == std::string_view::npos) break;
            position = end + 1;
        }
    }

    display("\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l\x1b[?2004l");
    if (mode_1000) display("\x1b[?1000h");
    if (mode_1002) display("\x1b[?1002h");
    if (mode_1003) display("\x1b[?1003h");
    if (sgr) display("\x1b[?1006h");
    if (bracketed) display("\x1b[?2004h");

    const std::string modes = modes_text(mode_1000, mode_1002, mode_1003, sgr, bracketed);
    log_line("CMD|MODE|modes=" + modes);
    show_modes(modes);
    show_status("MODE-APPLIED " + modes);
}

void apply_cursor_command(bool application)
{
    display(application ? "\x1b[?1h" : "\x1b[?1l");
    log_line(std::string("CMD|CURSOR|app=") + (application ? "1" : "0"));
    show_status(application ? "CURSOR=APP" : "CURSOR=NORMAL");
}

void print_history()
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const HANDLE output = ::GetStdHandle(STD_OUTPUT_HANDLE);
    int rows = 24;
    if (::GetConsoleScreenBufferInfo(output, &info)) {
        rows = static_cast<int>(info.srWindow.Bottom - info.srWindow.Top + 1);
    }
    rows = std::max(12, rows);
    // The visible screen after N = rows + 8 lines starts at line 9. The text
    // alternates every other line, so one three-line wheel notch always changes
    // the top row from blank to text (or the reverse), independent of small
    // console-window differences.
    const int lines = rows + 8;
    display("\x1b[2J\x1b[H");
    for (int line = 1; line <= lines; ++line) {
        if (line % 2 == 0) display("SCROLL-OLDER-ROW");
        display("\r\n");
    }
    // A visible marker on the last row lets the harness wait until the whole
    // history frame has reached the View before it measures the top row.
    display("\x1b[" + std::to_string(rows) + ";1H\x1b[2KHISTORY-READY");
    log_line("CMD|HISTORY|rows=" + std::to_string(rows) + "|lines=" + std::to_string(lines));
    log_line("CMD|HISTORY-READY");
}

void apply_command(const std::string& command)
{
    log_line("CMD|" + command);
    if (command.rfind("MODE=", 0) == 0) {
        apply_mode_command(command.substr(5));
    } else if (command == "CURSOR=APP") {
        apply_cursor_command(true);
    } else if (command == "CURSOR=NORMAL") {
        apply_cursor_command(false);
    } else if (command == "HISTORY") {
        print_history();
    } else {
        show_status("UNKNOWN-COMMAND " + command);
    }
}

void handle_text(std::string_view text, bool& in_command, std::string& command,
                 std::string& text_run)
{
    for (const char character : text) {
        if (in_command) {
            if (character == '~') {
                in_command = false;
                apply_command(command);
                command.clear();
            } else {
                command.push_back(character);
            }
        } else if (character == '~') {
            if (!text_run.empty()) {
                log_line("TEXT|hex=" + hex_encode(text_run));
                show_text(text_run);
                text_run.clear();
            }
            in_command = true;
            command.clear();
        } else {
            text_run.push_back(character);
        }
    }
    if (!text_run.empty() && !in_command) {
        log_line("TEXT|hex=" + hex_encode(text_run));
        show_text(text_run);
        text_run.clear();
    }
}

bool parse_sgr_body(std::string_view body, int& button, int& column, int& row)
{
    int values[3]{};
    int count = 0;
    size_t position = 0;
    while (position < body.size() && count < 3) {
        bool any = false;
        int value = 0;
        while (position < body.size() && body[position] >= '0' && body[position] <= '9') {
            value = value * 10 + (body[position] - '0');
            any = true;
            ++position;
        }
        if (!any) return false;
        values[count++] = value;
        if (position < body.size() && body[position] == ';') {
            ++position;
        } else {
            break;
        }
    }
    if (count != 3 || position != body.size()) return false;
    button = values[0];
    column = values[1];
    row = values[2];
    return true;
}

void handle_sgr(std::string_view body, char final)
{
    int button = 0;
    int column = 0;
    int row = 0;
    if (!parse_sgr_body(body, button, column, row)) {
        log_line("EVENT|sgr|malformed");
        return;
    }
    const bool press = final == 'M';
    log_line("EVENT|sgr|b=" + std::to_string(button) + "|x=" + std::to_string(column) +
             "|y=" + std::to_string(row) + "|press=" + (press ? "1" : "0"));
    show_mouse("SGR b=" + std::to_string(button) + " x=" + std::to_string(column) +
               " y=" + std::to_string(row) + " press=" + (press ? "1" : "0"));
}

void handle_x10(unsigned char button_byte, unsigned char column_byte, unsigned char row_byte)
{
    const int raw = static_cast<int>(button_byte) - 32;
    const int column = static_cast<int>(column_byte) - 32;
    const int row = static_cast<int>(row_byte) - 32;
    const bool press = (raw & 3) != 3;
    log_line("EVENT|x10|b=" + std::to_string(raw) + "|x=" + std::to_string(column) +
             "|y=" + std::to_string(row) + "|press=" + (press ? "1" : "0"));
    show_mouse("X10 b=" + std::to_string(raw) + " x=" + std::to_string(column) +
               " y=" + std::to_string(row) + " press=" + (press ? "1" : "0"));
}

void handle_paste(std::string_view payload)
{
    log_line("PASTE|bracket=1|hex=" + hex_encode(payload));
    show_paste("bracket=1 len=" + std::to_string(payload.size()) +
               " text=" + visible_escape(payload));
}

void process(std::string& pending, bool& in_command, std::string& command,
             std::string& text_run)
{
    size_t position = 0;
    while (position < pending.size()) {
        if (pending[position] != '\x1b') {
            size_t end = pending.find('\x1b', position);
            if (end == std::string::npos) end = pending.size();
            handle_text(std::string_view(pending).substr(position, end - position), in_command,
                        command, text_run);
            position = end;
            continue;
        }

        const std::string_view rest = std::string_view(pending).substr(position);

        if (rest.rfind("\x1b[<", 0) == 0) {
            const size_t final = pending.find_first_of("Mm", position + 3);
            if (final == std::string::npos) break;
            const std::string body = pending.substr(position + 3, final - (position + 3));
            handle_sgr(body, pending[final]);
            position = final + 1;
            continue;
        }
        if (rest.rfind("\x1b[M", 0) == 0) {
            if (rest.size() < 6) break;
            handle_x10(static_cast<unsigned char>(pending[position + 3]),
                       static_cast<unsigned char>(pending[position + 4]),
                       static_cast<unsigned char>(pending[position + 5]));
            position += 6;
            continue;
        }
        if (rest.rfind("\x1b[200~", 0) == 0) {
            const size_t end = pending.find("\x1b[201~", position + 6);
            if (end == std::string::npos) break;
            handle_paste(std::string_view(pending).substr(position + 6, end - (position + 6)));
            position = end + 6;
            continue;
        }
        // SS3 sequences (application cursor keys, F1-F4) are keys. Consume the
        // whole sequence so it cannot be mistaken for pasted text.
        if (rest.size() >= 3 && rest[1] == 'O') {
            log_line("KEY|hex=" + hex_encode(rest.substr(0, 3)));
            position += 3;
            continue;
        }
        // Any other CSI sequence is a key, not pasted text. Consume it whole so
        // the final '~' in e.g. ESC[2~ cannot start a ~COMMAND~ marker.
        if (rest.size() >= 2 && rest[1] == '[') {
            size_t final = 2;
            while (final < rest.size() &&
                   !(rest[final] >= 0x40 && rest[final] <= 0x7e)) {
                ++final;
            }
            if (final >= rest.size()) break;
            log_line("KEY|hex=" + hex_encode(rest.substr(0, final + 1)));
            position += final + 1;
            continue;
        }

        if (rest.size() < 6) break;
        // Unknown escape. Treat the ESC as text so the rest of the buffer is
        // not lost; the probe only needs the sequences it enables.
        handle_text("\x1b", in_command, command, text_run);
        ++position;
    }
    if (position > 0) pending.erase(0, position);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring log_path = L"t12-input.log";
    if (argc > 1) log_path = argv[1];
    g_log = ::CreateFileW(log_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_log == INVALID_HANDLE_VALUE) return 3;

    ::SetConsoleOutputCP(CP_UTF8);
    const HANDLE input = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD old_mode = 0;
    const bool got_mode = ::GetConsoleMode(input, &old_mode) != FALSE;
    const bool vt_input = got_mode && ::SetConsoleMode(input, ENABLE_VIRTUAL_TERMINAL_INPUT) != FALSE;
    if (!vt_input) {
        display("T12 INPUT PROBE: VT INPUT FAILED\r\n");
        return 4;
    }

    display("\x1b[2J\x1b[H");
    set_line(kRowTitle, "T12 INPUT PROBE");
    set_line(kRowModes, "MODES=OFF");
    set_line(kRowAlpha, "SELECT-ALPHA alpha bravo charlie delta echo");
    set_line(kRowBeta, "SELECT-BETA one two three four five six");
    set_line(kRowGamma, "SELECT-GAMMA red green blue yellow");
    set_line(kRowMouse, "LAST-MOUSE=NONE");
    set_line(kRowPaste, "LAST-PASTE=NONE");
    set_line(kRowText, "LAST-TEXT=NONE");
    set_line(kRowStatus, "STATUS=READY");
    show_modes(modes_text(false, false, false, false, false));
    log_line("READY|vt_input=1");

    std::string pending;
    std::string command;
    std::string text_run;
    bool in_command = false;
    for (;;) {
        char buffer[512];
        DWORD read = 0;
        if (!::ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
            log_line("READ-FAILED|error=" + std::to_string(::GetLastError()));
            break;
        }
        log_line("CHUNK|hex=" + hex_encode(std::string_view(buffer, read)));
        pending.append(buffer, read);
        process(pending, in_command, command, text_run);
    }
    ::CloseHandle(g_log);
    g_log = INVALID_HANDLE_VALUE;
    return 0;
}
