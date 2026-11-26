/*
 * C-only characterization test for the public ABI.
 *
 * Exercises the installed C interface without using the C++ wrapper.
 */
#include <termole/termole.h>

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#ifdef TERMOLE_TEST_GHOSTTY_FAILURE
#include <ghostty/vt.h>

/* Fail only the querying thread, leaving the ConPTY reader and real VT live. */
static _Thread_local int fail_active_screen;
GhosttyResult __real_ghostty_terminal_get(GhosttyTerminal, GhosttyTerminalData, void*);
GhosttyResult __wrap_ghostty_terminal_get(GhosttyTerminal terminal, GhosttyTerminalData data,
                                          void* out)
{
    if (fail_active_screen && data == GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN)
        return GHOSTTY_INVALID_VALUE;
    return __real_ghostty_terminal_get(terminal, data, out);
}
#endif

static void check(int condition, const char* what)
{
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

static int wait_for_text(termole_session* session, const char* expected)
{
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        char* text = NULL;
        if (termole_session_get_screen_text(session, &text, NULL) != TERMOLE_STATUS_OK) return 0;
        const int found = strstr(text, expected) != NULL;
        termole_free(text);
        if (found) return 1;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    return 0;
}

int main(void)
{
    check(termole_abi_version() == TERMOLE_ABI_VERSION, "abi version matches the header");
    check(termole_last_error() != NULL, "last_error is never NULL");
    check(strlen(termole_last_error()) == 0, "last_error starts empty");

    /* Invalid options and out pointer must not allocate or crash. */
    termole_session* session = (termole_session*)(uintptr_t)1;
    check(termole_session_create(NULL, &session) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "NULL options is rejected");
    check(session == NULL, "out_session is cleared on NULL options");

    check(termole_session_create(NULL, NULL) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "NULL out_session is rejected");

    termole_session_options options = TERMOLE_SESSION_OPTIONS_INIT;
    options.size = 4;
    session = (termole_session*)(uintptr_t)1;
    check(termole_session_create(&options, &session) == TERMOLE_STATUS_ABI_MISMATCH,
          "too-small options are rejected");
    check(session == NULL, "out_session is cleared on ABI mismatch");
    check(strlen(termole_last_error()) > 0, "ABI mismatch leaves a diagnostic");

    options = (termole_session_options)TERMOLE_SESSION_OPTIONS_INIT;
    options.abi_version = 999;
    check(termole_session_create(&options, &session) == TERMOLE_STATUS_ABI_MISMATCH,
          "wrong abi_version is rejected");

    termole_free(NULL); /* documented no-op */

    /* Synchronize on child output before sampling each screen. */
    options = (termole_session_options)TERMOLE_SESSION_OPTIONS_INIT;
    options.program = L"cmd.exe";
    const wchar_t* argv[] = { L"/d", L"/q", L"/c",
        L"echo PRIMARY-READY & pause >nul & echo \x1b[?1049hALT-READY & "
        L"pause >nul & echo \x1b[?1049l & exit /b 3" };
    options.arguments = argv;
    options.argument_count = 4;
    options.columns = 40;
    options.rows = 10;
    session = NULL;
    check(termole_session_create(&options, &session) == TERMOLE_STATUS_OK,
          "C consumer can create a Session");
    if (session) {
        check(termole_session_is_running(session, &(int){ 0 }) == TERMOLE_STATUS_OK,
              "running query succeeds");
#ifdef TERMOLE_TEST_GHOSTTY_FAILURE
        const termole_modes sentinel = { 9, 8, 7, 6, 5 };
        termole_modes incomplete = sentinel;
        fail_active_screen = 1;
        check(termole_session_get_modes(session, &incomplete) == TERMOLE_STATUS_INVALID_STATE &&
                  memcmp(&incomplete, &sentinel, sizeof(sentinel)) == 0 &&
                  strlen(termole_last_error()) != 0,
              "failed active-screen query reports an error and preserves all output fields");
        fail_active_screen = 0;
#endif
        termole_modes modes;
        check(wait_for_text(session, "PRIMARY-READY"), "child is on its primary screen");
        check(termole_session_get_modes(session, &modes) == TERMOLE_STATUS_OK &&
                  modes.alternate_screen == 0, "C consumer sees the initial primary screen");
        check(termole_session_write(session, "\r", 1) == TERMOLE_STATUS_OK, "enter alternate screen");
        check(wait_for_text(session, "ALT-READY"), "child has switched to the alternate screen");
        check(termole_session_get_modes(session, &modes) == TERMOLE_STATUS_OK && modes.alternate_screen == 1,
              "C consumer sees the active alternate screen");
        check(termole_session_write(session, "\r", 1) == TERMOLE_STATUS_OK, "resume child");
        for (int i = 0; i < 200; ++i) {
            int running = 0;
            if (termole_session_is_running(session, &running) != TERMOLE_STATUS_OK) break;
            if (!running) break;
            Sleep(50);
        }
        int32_t code = -1;
        check(termole_session_get_exit_code(session, &code) == TERMOLE_STATUS_OK,
              "exit code is available after drain");
        check(code == 3, "child exit code is preserved");
        uint16_t columns = 0;
        uint16_t rows = 0;
        check(termole_session_get_size(session, &columns, &rows) == TERMOLE_STATUS_OK,
              "snapshot size query succeeds");
        uint16_t cursor_column = 0;
        uint16_t cursor_row = 0;
        int cursor_visible = 0;
        check(termole_session_get_cursor(session, &cursor_column, &cursor_row, &cursor_visible) ==
                  TERMOLE_STATUS_OK,
              "cursor query succeeds");
        check(termole_session_get_modes(session, &modes) == TERMOLE_STATUS_OK && !modes.alternate_screen,
              "C consumer sees the restored primary screen");
        size_t history_lines = 0;
        check(termole_session_get_scrollback_line_count(session, &history_lines) ==
                  TERMOLE_STATUS_OK,
              "scrollback count query succeeds");
        char* selected = NULL;
        size_t selected_size = 0;
        check(termole_session_get_selection_text(session, 0, 0, 0, 0, &selected, &selected_size) ==
                  TERMOLE_STATUS_OK,
              "selection text query succeeds");
        termole_free(selected);
        size_t required = 0;
        check(termole_session_snapshot(session, 0, NULL, 0, &required) == TERMOLE_STATUS_OK,
              "snapshot size probe succeeds");
        check(required == (size_t)columns * rows, "snapshot required cells match the grid");
        termole_cell* cells = (termole_cell*)malloc(required * sizeof(termole_cell));
        check(cells != NULL, "snapshot buffer allocated");
        if (cells) {
            check(termole_session_snapshot(session, 0, cells, required, &required) ==
                      TERMOLE_STATUS_OK,
                  "C consumer can read the frame cells");
            check(cells[0].codepoint_count <= TERMOLE_CELL_MAX_CODEPOINTS,
                  "snapshot cell codepoints are bounded");
            free(cells);
        }
        termole_session_destroy(session);
    }

    if (failures != 0) {
        fprintf(stderr, "%d C ABI characterization check(s) failed\n", failures);
        return 1;
    }
    puts("PASS: C ABI characterization");
    return 0;
}
