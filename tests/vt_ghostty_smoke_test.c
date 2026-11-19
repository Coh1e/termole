/*
 * Contract smoke test for the vendored libghostty-vt static library.
 *
 * This is the first failing-test step of the VT migration: it proves the
 * headers and Windows gnu static library link under MinGW and that a minimal
 * parse -> render-state -> cell-text path works. It does not use Termole's
 * adapter yet.
 */
#include <ghostty/vt.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what)
{
    if (condition) return;
    fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
}

int main(void)
{
    GhosttyTerminal terminal = NULL;
    GhosttyRenderState render = NULL;
    GhosttyRenderStateRowIterator rows = NULL;
    GhosttyRenderStateRowCells cells = NULL;

    check(ghostty_terminal_new(NULL, &terminal, 20, 4) == GHOSTTY_SUCCESS,
          "terminal_new");
    if (!terminal) return 1;

    const char* text = "Hi\033[1;32mX\033[0m";
    ghostty_terminal_vt_write(terminal, (const uint8_t*)text, strlen(text));

    check(ghostty_render_state_new(NULL, &render) == GHOSTTY_SUCCESS,
          "render_state_new");
    check(ghostty_render_state_update(render, terminal) == GHOSTTY_SUCCESS,
          "render_state_update");

    uint16_t cols = 0;
    uint16_t row_count = 0;
    check(ghostty_render_state_get(render, GHOSTTY_RENDER_STATE_DATA_COLS, &cols) ==
              GHOSTTY_SUCCESS,
          "render cols");
    check(ghostty_render_state_get(render, GHOSTTY_RENDER_STATE_DATA_ROWS, &row_count) ==
              GHOSTTY_SUCCESS,
          "render rows");
    check(cols == 20 && row_count == 4, "render dimensions match the terminal");

    check(ghostty_render_state_row_iterator_new(NULL, &rows) == GHOSTTY_SUCCESS,
          "row_iterator_new");
    check(ghostty_render_state_get(render, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &rows) ==
              GHOSTTY_SUCCESS,
          "row_iterator populate");
    check(ghostty_render_state_row_cells_new(NULL, &cells) == GHOSTTY_SUCCESS,
          "row_cells_new");

    char line[64] = { 0 };
    size_t line_len = 0;
    if (ghostty_render_state_row_iterator_next(rows)) {
        check(ghostty_render_state_row_get(rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) ==
                  GHOSTTY_SUCCESS,
              "row_get cells");
        while (ghostty_render_state_row_cells_next(cells)) {
            uint8_t bytes[16];
            GhosttyBuffer buffer = { bytes, sizeof(bytes), 0 };
            if (ghostty_render_state_row_cells_get(
                    cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_UTF8, &buffer) !=
                GHOSTTY_SUCCESS) {
                continue;
            }
            if (line_len + buffer.len < sizeof(line)) {
                memcpy(line + line_len, bytes, buffer.len);
                line_len += buffer.len;
            }
        }
        line[line_len] = '\0';
    }

    check(strcmp(line, "HiX") == 0, "first row text is HiX");
    if (strcmp(line, "HiX") != 0) fprintf(stderr, "  row text: [%s]\n", line);

    ghostty_render_state_row_cells_free(cells);
    ghostty_render_state_row_iterator_free(rows);
    ghostty_render_state_free(render);
    ghostty_terminal_free(terminal);

    if (failures != 0) {
        fprintf(stderr, "%d libghostty-vt smoke check(s) failed\n", failures);
        return 1;
    }
    puts("PASS: libghostty-vt smoke");
    return 0;
}
