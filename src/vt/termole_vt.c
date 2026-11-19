#include <termole/core.h>

#include <ghostty/vt.h>

#include <stdlib.h>
#include <string.h>

struct termole_vt {
    GhosttyTerminal terminal;
    GhosttyRenderState render;
    GhosttyRenderStateRowIterator rows;
    GhosttyRenderStateRowCells cells;
    termole_frame frame;
    uint16_t columns;
    uint16_t rows_count;
    termole_vt_callbacks callbacks;
    GhosttyRenderStateColors colors;
};

static uint32_t pack_rgb(GhosttyColorRgb rgb)
{
    return ((uint32_t)rgb.r << 16) | ((uint32_t)rgb.g << 8) | (uint32_t)rgb.b;
}

static uint32_t resolve_color(GhosttyStyleColor color, const GhosttyRenderStateColors* colors,
                              uint32_t fallback, uint8_t* is_default)
{
    if (color.tag == GHOSTTY_STYLE_COLOR_NONE) {
        *is_default = 1;
        return fallback;
    }
    *is_default = 0;
    if (color.tag == GHOSTTY_STYLE_COLOR_PALETTE) {
        return pack_rgb(colors->palette[color.value.palette]);
    }
    if (color.tag == GHOSTTY_STYLE_COLOR_RGB) {
        return pack_rgb(color.value.rgb);
    }
    *is_default = 1;
    return fallback;
}

static uint16_t map_attributes(GhosttyStyleFlags flags)
{
    uint16_t attributes = 0;
    if (flags & GHOSTTY_STYLE_FLAG_BOLD) attributes |= TERMOLE_CELL_BOLD;
    if (flags & GHOSTTY_STYLE_FLAG_ITALIC) attributes |= TERMOLE_CELL_ITALIC;
    if (flags & GHOSTTY_STYLE_FLAG_FAINT) attributes |= TERMOLE_CELL_FAINT;
    if (flags & GHOSTTY_STYLE_FLAG_INVERSE) attributes |= TERMOLE_CELL_INVERSE;
    if (flags & GHOSTTY_STYLE_FLAG_INVISIBLE) attributes |= TERMOLE_CELL_INVISIBLE;
    if (flags & GHOSTTY_STYLE_FLAG_STRIKETHROUGH) attributes |= TERMOLE_CELL_STRIKETHROUGH;
    if (((flags & GHOSTTY_STYLE_UNDERLINE_MASK) >> GHOSTTY_STYLE_UNDERLINE_SHIFT) != 0)
        attributes |= TERMOLE_CELL_UNDERLINE;
    return attributes;
}

static int ensure_cells(termole_vt* vt, uint16_t columns, uint16_t rows)
{
    const size_t count = (size_t)columns * rows;
    termole_cell* cells = realloc(vt->frame.cells, count * sizeof(*cells));
    if (!cells) return 0;
    memset(cells, 0, count * sizeof(*cells));
    vt->frame.cells = cells;
    vt->frame.columns = columns;
    vt->frame.rows = rows;
    return 1;
}

termole_core_status termole_vt_create(uint16_t columns, uint16_t rows, termole_vt** out_vt)
{
    if (!out_vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    *out_vt = NULL;
    if (columns == 0 || rows == 0) return TERMOLE_CORE_INVALID_ARGUMENT;

    termole_vt* vt = calloc(1, sizeof(*vt));
    if (!vt) return TERMOLE_CORE_OUT_OF_MEMORY;
    if (ghostty_terminal_new(NULL, &vt->terminal, columns, rows) != GHOSTTY_SUCCESS) goto fail;
    const size_t history_limit = 2000;
    (void)ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_SCROLLBACK_MAX_LINES, &history_limit);
    if (ghostty_render_state_new(NULL, &vt->render) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_render_state_row_iterator_new(NULL, &vt->rows) != GHOSTTY_SUCCESS) goto fail;
    if (ghostty_render_state_row_cells_new(NULL, &vt->cells) != GHOSTTY_SUCCESS) goto fail;
    vt->colors = (GhosttyRenderStateColors)GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    (void)ghostty_render_state_update(vt->render, vt->terminal);
    (void)ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_COLORS, &vt->colors);
    if (!ensure_cells(vt, columns, rows)) goto fail;
    vt->columns = columns;
    vt->rows_count = rows;
    *out_vt = vt;
    return TERMOLE_CORE_OK;

fail:
    termole_vt_free(vt);
    return TERMOLE_CORE_OUT_OF_MEMORY;
}

void termole_vt_free(termole_vt* vt)
{
    if (!vt) return;
    free(vt->frame.cells);
    ghostty_render_state_row_cells_free(vt->cells);
    ghostty_render_state_row_iterator_free(vt->rows);
    ghostty_render_state_free(vt->render);
    ghostty_terminal_free(vt->terminal);
    free(vt);
}

termole_core_status termole_vt_feed(termole_vt* vt, const void* data, size_t size)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (!data && size != 0) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (size != 0) {
        ghostty_terminal_vt_write(vt->terminal, (const uint8_t*)data, size);
        vt->frame.serial = 0; /* invalidate the previous frame */
    }
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_resize(termole_vt* vt, uint16_t columns, uint16_t rows)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (columns == 0 || rows == 0) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (ghostty_terminal_resize(vt->terminal, columns, rows, 8, 16) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    if (!ensure_cells(vt, columns, rows)) return TERMOLE_CORE_OUT_OF_MEMORY;
    vt->columns = columns;
    vt->rows_count = rows;
    vt->frame.serial = 0;
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_frame(termole_vt* vt, const termole_frame** out_frame)
{
    if (!vt || !out_frame) return TERMOLE_CORE_INVALID_ARGUMENT;
    *out_frame = NULL;

    if (ghostty_render_state_update(vt->render, vt->terminal) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    if (ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_COLORS, &colors) !=
        GHOSTTY_SUCCESS) {
        memset(&colors, 0, sizeof(colors));
    }
    vt->colors = colors;
    const uint32_t default_fg = pack_rgb(colors.foreground);
    const uint32_t default_bg = pack_rgb(colors.background);

    uint16_t columns = 0;
    uint16_t rows = 0;
    ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_COLS, &columns);
    ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
    if (columns == 0 || rows == 0) return TERMOLE_CORE_INVALID_STATE;
    if (columns != vt->frame.columns || rows != vt->frame.rows) {
        if (!ensure_cells(vt, columns, rows)) return TERMOLE_CORE_OUT_OF_MEMORY;
        vt->columns = columns;
        vt->rows_count = rows;
    }

    GhosttyRenderStateDirty dirty = GHOSTTY_RENDER_STATE_DIRTY_FULL;
    ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_DIRTY, &dirty);
    vt->frame.dirty = (uint8_t)dirty;

    bool cursor_visible = false;
    bool cursor_has_value = false;
    uint16_t cursor_x = 0;
    uint16_t cursor_y = 0;
    ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                             &cursor_visible);
    ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE,
                             &cursor_has_value);
    if (cursor_has_value) {
        ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X,
                                 &cursor_x);
        ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y,
                                 &cursor_y);
    }
    vt->frame.cursor_visible = cursor_visible ? 1u : 0u;
    vt->frame.cursor_column = cursor_x;
    vt->frame.cursor_row = cursor_y;

    if (ghostty_render_state_get(vt->render, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                 &vt->rows) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;

    uint16_t row = 0;
    while (row < rows && ghostty_render_state_row_iterator_next(vt->rows)) {
        if (ghostty_render_state_row_get(vt->rows, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS,
                                         &vt->cells) != GHOSTTY_SUCCESS)
            break;
        uint16_t column = 0;
        while (column < columns && ghostty_render_state_row_cells_next(vt->cells)) {
            termole_cell* out = &vt->frame.cells[(size_t)row * columns + column];

            GhosttyCell raw = 0;
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            bool has_text = false;
            ghostty_render_state_row_cells_get(vt->cells,
                                               GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
            ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &has_text);

            uint32_t codepoints[TERMOLE_CELL_MAX_CODEPOINTS];
            GhosttyCodepointBuffer graphemes = { codepoints, TERMOLE_CELL_MAX_CODEPOINTS, 0 };
            ghostty_render_state_row_cells_get(
                vt->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, &graphemes);
            const size_t count = graphemes.len < TERMOLE_CELL_MAX_CODEPOINTS
                                     ? graphemes.len
                                     : TERMOLE_CELL_MAX_CODEPOINTS;
            memcpy(out->codepoints, codepoints, count * sizeof(uint32_t));
            out->codepoint_count = (uint8_t)count;
            out->width = wide == GHOSTTY_CELL_WIDE_WIDE ? 2u : 1u;
            out->continuation = (!has_text && column > 0 &&
                                 vt->frame.cells[(size_t)row * columns + column - 1].width == 2)
                                    ? 1u
                                    : 0u;

            GhosttyStyle style;
            memset(&style, 0, sizeof(style));
            ghostty_render_state_row_cells_get(
                vt->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            out->attributes = map_attributes(style.flags);
            out->foreground =
                resolve_color(style.fg_color, &colors, default_fg, &out->foreground_is_default);
            out->background =
                resolve_color(style.bg_color, &colors, default_bg, &out->background_is_default);
            bool selected = false;
            ghostty_render_state_row_cells_get(
                vt->cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_SELECTED, &selected);
            out->selected = selected ? 1u : 0u;

            ++column;
        }
        ++row;
    }

    ++vt->frame.serial;
    *out_frame = &vt->frame;
    return TERMOLE_CORE_OK;
}



static void on_write_pty(GhosttyTerminal terminal, void* userdata, const uint8_t* data, size_t size)
{
    (void)terminal;
    termole_vt* vt = userdata;
    if (vt && vt->callbacks.write_pty) vt->callbacks.write_pty(vt->callbacks.userdata, data, size);
}

static void on_bell(GhosttyTerminal terminal, void* userdata)
{
    (void)terminal;
    termole_vt* vt = userdata;
    if (vt && vt->callbacks.bell) vt->callbacks.bell(vt->callbacks.userdata);
}

static void on_title_changed(GhosttyTerminal terminal, void* userdata)
{
    (void)terminal;
    termole_vt* vt = userdata;
    if (vt && vt->callbacks.title_changed) vt->callbacks.title_changed(vt->callbacks.userdata);
}

termole_core_status termole_vt_set_callbacks(termole_vt* vt, const termole_vt_callbacks* callbacks)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (callbacks) {
        vt->callbacks = *callbacks;
    } else {
        memset(&vt->callbacks, 0, sizeof(vt->callbacks));
    }
    ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_USERDATA, vt);
    ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_WRITE_PTY,
                         vt->callbacks.write_pty ? (const void*)on_write_pty : NULL);
    ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_BELL,
                         vt->callbacks.bell ? (const void*)on_bell : NULL);
    ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_TITLE_CHANGED,
                         vt->callbacks.title_changed ? (const void*)on_title_changed : NULL);
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_history_rows(termole_vt* vt, uint32_t* out_rows)
{
    if (!vt || !out_rows) return TERMOLE_CORE_INVALID_ARGUMENT;
    size_t rows = 0;
    if (ghostty_terminal_get(vt->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &rows) !=
        GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    if (rows > 1000) rows = 1000;
    *out_rows = (uint32_t)rows;
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_cell(termole_vt* vt, int32_t row, int32_t column,
                                    termole_cell* out_cell)
{
    if (!vt || !out_cell) return TERMOLE_CORE_INVALID_ARGUMENT;
    if (row < 0 || column < 0 || row > UINT16_MAX || column > UINT16_MAX)
        return TERMOLE_CORE_INVALID_ARGUMENT;
    memset(out_cell, 0, sizeof(*out_cell));

    size_t actual_history = 0;
    (void)ghostty_terminal_get(vt->terminal, GHOSTTY_TERMINAL_DATA_SCROLLBACK_ROWS, &actual_history);
    const size_t visible_history = actual_history > 1000 ? 1000 : actual_history;
    const uint32_t actual_row = (uint32_t)(actual_history - visible_history + (size_t)row);
    GhosttyPoint point = {
        .tag = GHOSTTY_POINT_TAG_SCREEN,
        .value = { .coordinate = { .x = (uint16_t)column, .y = actual_row } },
    };
    GhosttyGridRef ref;
    if (ghostty_terminal_grid_ref(vt->terminal, point, &ref) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_ARGUMENT;

    GhosttyCell raw = 0;
    GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
    bool has_text = false;
    if (ghostty_grid_ref_cell(&ref, &raw) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
    ghostty_cell_get(raw, GHOSTTY_CELL_DATA_HAS_TEXT, &has_text);

    uint32_t codepoints[TERMOLE_CELL_MAX_CODEPOINTS];
    size_t count = 0;
    if (ghostty_grid_ref_graphemes(&ref, codepoints, TERMOLE_CELL_MAX_CODEPOINTS, &count) !=
            GHOSTTY_SUCCESS &&
        count == 0) {
        has_text = false;
    }
    if (count > TERMOLE_CELL_MAX_CODEPOINTS) count = TERMOLE_CELL_MAX_CODEPOINTS;
    memcpy(out_cell->codepoints, codepoints, count * sizeof(uint32_t));
    out_cell->codepoint_count = (uint8_t)count;
    out_cell->width = wide == GHOSTTY_CELL_WIDE_WIDE ? 2u : 1u;
    out_cell->continuation = (!has_text && count == 0 && column > 0) ? 1u : 0u;

    GhosttyStyle style;
    memset(&style, 0, sizeof(style));
    ghostty_grid_ref_style(&ref, &style);
    out_cell->attributes = map_attributes(style.flags);
    out_cell->foreground = resolve_color(style.fg_color, &vt->colors, pack_rgb(vt->colors.foreground),
                                        &out_cell->foreground_is_default);
    out_cell->background = resolve_color(style.bg_color, &vt->colors, pack_rgb(vt->colors.background),
                                        &out_cell->background_is_default);
    return TERMOLE_CORE_OK;
}
static int query_mode(termole_vt* vt, GhosttyMode mode)
{
    GhosttyTerminalModeConfig config = { .mode = mode, .value = false };
    if (ghostty_terminal_get(vt->terminal, GHOSTTY_TERMINAL_DATA_MODE, &config) != GHOSTTY_SUCCESS)
        return 0;
    return config.value ? 1 : 0;
}

termole_core_status termole_vt_modes(termole_vt* vt, termole_modes* out_modes)
{
    if (!vt || !out_modes) return TERMOLE_CORE_INVALID_ARGUMENT;
    memset(out_modes, 0, sizeof(*out_modes));
    if (query_mode(vt, GHOSTTY_MODE_MOUSE_EVENT_NORMAL)) out_modes->mouse_tracking = 1;
    else if (query_mode(vt, GHOSTTY_MODE_MOUSE_EVENT_BUTTON)) out_modes->mouse_tracking = 2;
    else if (query_mode(vt, GHOSTTY_MODE_MOUSE_EVENT_ANY)) out_modes->mouse_tracking = 3;
    out_modes->mouse_sgr = (uint8_t)query_mode(vt, GHOSTTY_MODE_MOUSE_FORMAT_SGR);
    out_modes->bracketed_paste = (uint8_t)query_mode(vt, GHOSTTY_MODE_BRACKETED_PASTE);
    out_modes->application_cursor = (uint8_t)query_mode(vt, GHOSTTY_MODE_CURSOR_KEYS);
    out_modes->alternate_screen = (uint8_t)query_mode(vt, GHOSTTY_MODE_ALT_SCREEN);
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_title(termole_vt* vt, char* buffer, size_t capacity,
                                     size_t* out_len)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    GhosttyString title = { 0 };
    if (ghostty_terminal_get(vt->terminal, GHOSTTY_TERMINAL_DATA_TITLE, &title) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    if (out_len) *out_len = title.len;
    if (!buffer || capacity < title.len + 1) return TERMOLE_CORE_OUT_OF_SPACE;
    if (title.len) memcpy(buffer, title.ptr, title.len);
    buffer[title.len] = '\0';
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_scroll(termole_vt* vt, int32_t lines)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    GhosttyTerminalScrollViewport behavior = {
        .tag = GHOSTTY_TERMINAL_SCROLL_VIEWPORT_DELTA,
        .value = { .delta = lines },
    };
    if (ghostty_terminal_scroll_viewport(vt->terminal, behavior) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    vt->frame.serial = 0;
    return TERMOLE_CORE_OK;
}

static int grid_ref_at(termole_vt* vt, int32_t row, int32_t column, GhosttyGridRef* out)
{
    if (row < 0 || column < 0 || row > UINT16_MAX || column > UINT16_MAX) return 0;
    GhosttyPoint point = {
        .tag = GHOSTTY_POINT_TAG_ACTIVE,
        .value = { .coordinate = { .x = (uint16_t)column, .y = (uint16_t)row } },
    };
    return ghostty_terminal_grid_ref(vt->terminal, point, out) == GHOSTTY_SUCCESS;
}

termole_core_status termole_vt_select(termole_vt* vt, int32_t start_row, int32_t start_column,
                                      int32_t end_row, int32_t end_column)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    GhosttySelection selection;
    memset(&selection, 0, sizeof(selection));
    if (!grid_ref_at(vt, start_row, start_column, &selection.start) ||
        !grid_ref_at(vt, end_row, end_column, &selection.end))
        return TERMOLE_CORE_INVALID_ARGUMENT;
    if (ghostty_terminal_set(vt->terminal, GHOSTTY_TERMINAL_OPT_SELECTION, &selection) !=
        GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    vt->frame.serial = 0;
    return TERMOLE_CORE_OK;
}

termole_core_status termole_vt_text(termole_vt* vt, char* buffer, size_t capacity, size_t* out_len)
{
    if (!vt) return TERMOLE_CORE_INVALID_ARGUMENT;
    GhosttyFormatter formatter = NULL;
    if (ghostty_formatter_terminal_new(NULL, &formatter, vt->terminal) != GHOSTTY_SUCCESS)
        return TERMOLE_CORE_INVALID_STATE;
    size_t needed = 0;
    GhosttyResult result = ghostty_formatter_format_buf(formatter, NULL, 0, &needed);
    if (result == GHOSTTY_SUCCESS) {
        if (out_len) *out_len = 0;
        if (buffer && capacity > 0) buffer[0] = '\0';
        ghostty_formatter_free(formatter);
        return TERMOLE_CORE_OK;
    }
    if (result != GHOSTTY_OUT_OF_SPACE) {
        ghostty_formatter_free(formatter);
        return TERMOLE_CORE_INVALID_STATE;
    }
    if (out_len) *out_len = needed;
    if (!buffer || capacity < needed + 1) {
        ghostty_formatter_free(formatter);
        return TERMOLE_CORE_OUT_OF_SPACE;
    }
    size_t written = 0;
    result = ghostty_formatter_format_buf(formatter, (uint8_t*)buffer, capacity, &written);
    ghostty_formatter_free(formatter);
    if (result != GHOSTTY_SUCCESS) return TERMOLE_CORE_INVALID_STATE;
    buffer[written] = '\0';
    if (out_len) *out_len = written;
    return TERMOLE_CORE_OK;
}
static size_t encode_utf8(uint32_t codepoint, char* out)
{
    if (codepoint <= 0x7F) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint <= 0x7FF) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint <= 0xFFFF) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

termole_core_status termole_cell_text(const termole_cell* cell, char* buffer, size_t capacity,
                                      size_t* out_len)
{
    if (!cell) return TERMOLE_CORE_INVALID_ARGUMENT;
    size_t required = 0;
    char bytes[4];
    for (uint8_t index = 0; index < cell->codepoint_count; ++index) {
        required += encode_utf8(cell->codepoints[index], bytes);
    }
    if (out_len) *out_len = required;
    if (!buffer || capacity < required + 1) return TERMOLE_CORE_OUT_OF_SPACE;
    size_t offset = 0;
    for (uint8_t index = 0; index < cell->codepoint_count; ++index) {
        offset += encode_utf8(cell->codepoints[index], buffer + offset);
    }
    buffer[offset] = '\0';
    return TERMOLE_CORE_OK;
}
