/*
 * Contract tests for the C VT adapter (termole_core.h).
 *
 * The adapter is the stable boundary a Go/Zig/Rust UI can consume. These tests
 * pin text, attributes, wide cells, frame lifetime and error behavior without
 * touching the C++ Session.
 */
#include <termole/core.h>

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what)
{
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

static void cell_text(const termole_frame* frame, uint16_t row, uint16_t column, char* out,
                      size_t capacity)
{
    size_t length = 0;
    out[0] = '\0';
    if (termole_cell_text(&frame->cells[(size_t)row * frame->columns + column], out, capacity,
                          &length) != TERMOLE_CORE_OK) {
        out[0] = '\0';
    }
}


typedef struct {
    char buffer[64];
    size_t length;
    int bells;
    int titles;
} callback_state;

static void on_write_pty(void* userdata, const void* data, size_t size)
{
    callback_state* state = userdata;
    if (state->length + size < sizeof(state->buffer)) {
        memcpy(state->buffer + state->length, data, size);
        state->length += size;
    }
}

static void on_bell(void* userdata) { ((callback_state*)userdata)->bells++; }
static void on_title(void* userdata) { ((callback_state*)userdata)->titles++; }
int main(void)
{
    /* Invalid arguments. */
    check(termole_vt_create(0, 1, NULL) == TERMOLE_CORE_INVALID_ARGUMENT,
          "null out_vt is rejected");
    termole_vt* vt = NULL;
    check(termole_vt_create(0, 1, &vt) == TERMOLE_CORE_INVALID_ARGUMENT,
          "zero columns is rejected");
    check(termole_vt_create(10, 2, &vt) == TERMOLE_CORE_OK, "create 10x2");
    check(vt != NULL, "vt handle");
    check(termole_vt_feed(vt, NULL, 1) == TERMOLE_CORE_INVALID_ARGUMENT,
          "NULL data with size is rejected");
    check(termole_vt_resize(vt, 0, 2) == TERMOLE_CORE_INVALID_ARGUMENT,
          "zero resize is rejected");
    termole_vt_free(vt);
    termole_vt_free(NULL);

    /* Text and style. */
    vt = NULL;
    check(termole_vt_create(20, 4, &vt) == TERMOLE_CORE_OK, "create 20x4");
    const char* text = "Hi\033[1;32mX\033[0m";
    check(termole_vt_feed(vt, text, strlen(text)) == TERMOLE_CORE_OK, "feed styled text");

    const termole_frame* frame = NULL;
    check(termole_vt_frame(vt, &frame) == TERMOLE_CORE_OK, "frame");
    if (frame) {
        check(frame->columns == 20 && frame->rows == 4, "frame dimensions");
        check(frame->cells != NULL, "frame cells");
        if (frame->cells) {
            char h[8], i[8], x[8];
            cell_text(frame, 0, 0, h, sizeof(h));
            cell_text(frame, 0, 1, i, sizeof(i));
            cell_text(frame, 0, 2, x, sizeof(x));
            check(strcmp(h, "H") == 0 && strcmp(i, "i") == 0 && strcmp(x, "X") == 0,
                  "first row is HiX");
            termole_cell via_cell;
            char via_text[8];
            size_t via_len = 0;
            check(termole_vt_cell(vt, 0, 0, &via_cell) == TERMOLE_CORE_OK,
                  "cell query active row");
            check(termole_cell_text(&via_cell, via_text, sizeof(via_text), &via_len) ==
                      TERMOLE_CORE_OK,
                  "cell text active row");
            check(strcmp(via_text, "H") == 0, "active cell 0,0 is H");
            check(termole_vt_cell(vt, 0, 3, &via_cell) == TERMOLE_CORE_OK &&
                      !via_cell.continuation && via_cell.codepoint_count == 0,
                  "blank columns are not wide-cell continuations");
            check((frame->cells[2].attributes & TERMOLE_CELL_BOLD) != 0, "X is bold");
            check(frame->cells[2].foreground_is_default == 0, "X has an explicit foreground");
            check(frame->cells[0].foreground_is_default == 1, "H uses the default foreground");
        }
    }

    /* Wide cells: 中 occupies two columns; the tail is a continuation. */
    termole_vt_free(vt);
    vt = NULL;
    check(termole_vt_create(10, 2, &vt) == TERMOLE_CORE_OK, "create 10x2 for wide");
    const char* wide = "\344\270\255"; /* U+4E2D */
    check(termole_vt_feed(vt, wide, strlen(wide)) == TERMOLE_CORE_OK, "feed wide char");
    check(termole_vt_frame(vt, &frame) == TERMOLE_CORE_OK, "frame after wide");
    if (frame) {
        check(frame->columns == 10 && frame->rows == 2, "resized frame dimensions");
        check(frame->cells[0].codepoint_count == 1 &&
                  frame->cells[0].codepoints[0] == 0x4E2D,
              "wide cell codepoint");
        check(frame->cells[0].width == 2, "wide cell width is 2");
        check(frame->cells[1].continuation == 1, "second cell is a continuation");
        termole_cell tail;
        check(termole_vt_cell(vt, 0, 1, &tail) == TERMOLE_CORE_OK && tail.continuation,
              "cell query identifies the wide-cell tail");
    }


    /* Modes, title and selection. */
    termole_vt_free(vt);
    vt = NULL;
    check(termole_vt_create(20, 4, &vt) == TERMOLE_CORE_OK, "create for modes");
    const char* modes_seq = "\033[?1000h\033[?1006h\033[?2004h";
    check(termole_vt_feed(vt, modes_seq, strlen(modes_seq)) == TERMOLE_CORE_OK, "feed modes");
    termole_modes modes;
    check(termole_vt_modes(vt, &modes) == TERMOLE_CORE_OK, "query modes");
    check(modes.mouse_tracking == 1, "mouse tracking enabled");
    check(modes.mouse_sgr == 1, "SGR mouse enabled");
    check(modes.bracketed_paste == 1, "bracketed paste enabled");

    const char* title_seq = "\033]0;Hello\007";
    check(termole_vt_feed(vt, title_seq, strlen(title_seq)) == TERMOLE_CORE_OK, "feed title");
    char title[32];
    size_t title_len = 0;
    check(termole_vt_title(vt, title, sizeof(title), &title_len) == TERMOLE_CORE_OK, "title");
    check(strcmp(title, "Hello") == 0, "title text");

    const char* hello = "hello";
    check(termole_vt_feed(vt, hello, strlen(hello)) == TERMOLE_CORE_OK, "feed hello");
    check(termole_vt_select(vt, 0, 0, 0, 4) == TERMOLE_CORE_OK, "select hello");
    check(termole_vt_frame(vt, &frame) == TERMOLE_CORE_OK, "frame after select");
    if (frame) {
        int all_selected = 1;
        for (int i = 0; i < 5; ++i) all_selected &= frame->cells[i].selected;
        check(all_selected == 1, "selected cells 0..4");
        check(frame->cells[5].selected == 0, "cell 5 is not selected");
    }

    /* Callbacks: DSR response goes to write_pty; BEL and title are reported. */
    callback_state cb = { { 0 }, 0, 0, 0 };
    termole_vt_callbacks callbacks = { &cb, on_write_pty, on_bell, on_title };
    check(termole_vt_set_callbacks(vt, &callbacks) == TERMOLE_CORE_OK, "set callbacks");
    const char* dsr = "\033[6n";
    check(termole_vt_feed(vt, dsr, strlen(dsr)) == TERMOLE_CORE_OK, "feed DSR");
    check(cb.length > 0 && memchr(cb.buffer, 'R', cb.length) != NULL,
          "DSR response written back through write_pty");
    check(termole_vt_feed(vt, "\a", 1) == TERMOLE_CORE_OK, "feed bell");
    check(cb.bells == 1, "bell callback");
    check(termole_vt_feed(vt, "\033]0;Callback\007", 14) == TERMOLE_CORE_OK, "feed callback title");
    check(cb.titles >= 1, "title changed callback");
    check(termole_vt_scroll(vt, -1) == TERMOLE_CORE_OK, "scroll up by one line");
    check(termole_vt_frame(vt, &frame) == TERMOLE_CORE_OK, "frame after scroll");
    char text_buffer[512];
    size_t text_len = 0;
    check(termole_vt_text(vt, text_buffer, sizeof(text_buffer), &text_len) == TERMOLE_CORE_OK,
          "plain text");
    check(strstr(text_buffer, "hello") != NULL, "plain text contains hello");


    /* Full-screen cell access and scrollback row count. */
    termole_vt_free(vt);
    vt = NULL;
    check(termole_vt_create(10, 2, &vt) == TERMOLE_CORE_OK, "create for history");
    const char* lines = "one\r\ntwo\r\nthree";
    check(termole_vt_feed(vt, lines, strlen(lines)) == TERMOLE_CORE_OK, "feed history lines");
    uint32_t history_rows = 0;
    check(termole_vt_history_rows(vt, &history_rows) == TERMOLE_CORE_OK, "history rows query");
    check(history_rows >= 1, "scrollback has at least one row");
    termole_cell first;
    check(termole_vt_cell(vt, 0, 0, &first) == TERMOLE_CORE_OK, "cell at screen row 0");
    char first_text[8];
    size_t first_len = 0;
    check(termole_cell_text(&first, first_text, sizeof(first_text), &first_len) == TERMOLE_CORE_OK,
          "cell text");
    check(strcmp(first_text, "o") == 0, "oldest scrollback row starts with o");
    termole_vt_free(vt);

    if (failures != 0) {
        fprintf(stderr, "%d VT adapter check(s) failed\n", failures);
        return 1;
    }
    puts("PASS: VT adapter contract");
    return 0;
}
