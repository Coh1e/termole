/*
 * Termole core VT ABI (C).
 *
 * This is the replaceable boundary between the C terminal core and any UI
 * layer (C today; Go/Zig/Rust later). No C++ types cross this header.
 *
 * Frame lifetime: the frame returned by termole_vt_frame() is owned by the
 * termole_vt instance and stays valid until the next call that mutates the
 * terminal or produces a new frame.
 */
#ifndef TERMOLE_CORE_H
#define TERMOLE_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERMOLE_CORE_ABI_VERSION 1u

typedef enum termole_core_status {
    TERMOLE_CORE_OK = 0,
    TERMOLE_CORE_INVALID_ARGUMENT = 1,
    TERMOLE_CORE_OUT_OF_MEMORY = 2,
    TERMOLE_CORE_INVALID_STATE = 3,
    TERMOLE_CORE_UNSUPPORTED = 4,
    TERMOLE_CORE_OUT_OF_SPACE = 5
} termole_core_status;

typedef enum termole_cell_attribute {
    TERMOLE_CELL_BOLD = 1u << 0,
    TERMOLE_CELL_UNDERLINE = 1u << 1,
    TERMOLE_CELL_INVERSE = 1u << 2,
    TERMOLE_CELL_FAINT = 1u << 3,
    TERMOLE_CELL_ITALIC = 1u << 4,
    TERMOLE_CELL_INVISIBLE = 1u << 5,
    TERMOLE_CELL_STRIKETHROUGH = 1u << 6
} termole_cell_attribute;

#define TERMOLE_CELL_MAX_CODEPOINTS 8u

typedef struct termole_cell {
    uint32_t codepoints[TERMOLE_CELL_MAX_CODEPOINTS];
    uint8_t codepoint_count;
    uint8_t width;       /* 1 or 2 cells */
    uint8_t continuation; /* 1 when this is the tail of a wide cell */
    uint8_t padding;
    uint16_t attributes; /* TERMOLE_CELL_* bits */
    uint8_t selected;
    uint8_t foreground_is_default;
    uint8_t background_is_default;
    uint32_t foreground; /* 0x00RRGGBB */
    uint32_t background; /* 0x00RRGGBB */
} termole_cell;

typedef struct termole_frame {
    uint16_t columns;
    uint16_t rows;
    termole_cell* cells; /* row-major, columns * rows; owned by the VT */
    uint16_t cursor_column;
    uint16_t cursor_row;
    uint8_t cursor_visible;
    uint8_t dirty; /* 0 = clean, 1 = partial, 2 = full */
    uint64_t serial;
} termole_frame;

typedef struct termole_vt termole_vt;

termole_core_status termole_vt_create(uint16_t columns, uint16_t rows, termole_vt** out_vt);
void termole_vt_free(termole_vt* vt);

termole_core_status termole_vt_feed(termole_vt* vt, const void* data, size_t size);
termole_core_status termole_vt_resize(termole_vt* vt, uint16_t columns, uint16_t rows);

/* Logical keys shared by the native views and the Windows SDK. */
typedef enum termole_key {
    TERMOLE_KEY_ENTER = 1,
    TERMOLE_KEY_TAB = 2,
    TERMOLE_KEY_BACKSPACE = 3,
    TERMOLE_KEY_ESCAPE = 4,
    TERMOLE_KEY_ARROW_UP = 5,
    TERMOLE_KEY_ARROW_DOWN = 6,
    TERMOLE_KEY_ARROW_LEFT = 7,
    TERMOLE_KEY_ARROW_RIGHT = 8,
    TERMOLE_KEY_HOME = 9,
    TERMOLE_KEY_END = 10,
    TERMOLE_KEY_DELETE = 11,
    TERMOLE_KEY_INSERT = 12,
    TERMOLE_KEY_PAGE_UP = 13,
    TERMOLE_KEY_PAGE_DOWN = 14,
    TERMOLE_KEY_CTRL_C = 15,
    TERMOLE_KEY_CTRL_D = 16
} termole_key;

typedef struct termole_modes {
    uint8_t mouse_tracking;
    uint8_t mouse_sgr;
    uint8_t bracketed_paste;
    uint8_t application_cursor;
    uint8_t alternate_screen;
} termole_modes;

typedef struct termole_vt_callbacks {
    void* userdata;
    void (*write_pty)(void* userdata, const void* data, size_t size);
    void (*bell)(void* userdata);
    void (*title_changed)(void* userdata);
} termole_vt_callbacks;

/* Installs optional I/O/effect callbacks. Borrowed; may be NULL. */
termole_core_status termole_vt_set_callbacks(termole_vt* vt,
                                             const termole_vt_callbacks* callbacks);

/* Queries the terminal modes mirrored from libghostty-vt. On failure the
 * caller's output is unchanged. */
termole_core_status termole_vt_modes(termole_vt* vt, termole_modes* out_modes);

/* Copies the OSC 0/2 title as UTF-8. Returns OUT_OF_SPACE with required size. */
termole_core_status termole_vt_title(termole_vt* vt, char* buffer, size_t capacity,
                                     size_t* out_len);

/* Scrolls the viewport: negative moves up (older rows), positive moves down. */
termole_core_status termole_vt_scroll(termole_vt* vt, int32_t lines);

/* Selects a range in active-screen cell coordinates (viewport rows). */
termole_core_status termole_vt_select(termole_vt* vt, int32_t start_row, int32_t start_column,
                                      int32_t end_row, int32_t end_column);

/* Copies the terminal as plain text (formatter, trailing whitespace trimmed).
 * Returns OUT_OF_SPACE with the required capacity when the buffer is too small. */
termole_core_status termole_vt_text(termole_vt* vt, char* buffer, size_t capacity,
                                    size_t* out_len);

/* Number of scrollback rows before the active viewport. */
termole_core_status termole_vt_history_rows(termole_vt* vt, uint32_t* out_rows);

/* Reads one cell in full-screen coordinates (0 = oldest scrollback row). */
termole_core_status termole_vt_cell(termole_vt* vt, int32_t row, int32_t column,
                                    termole_cell* out_cell);

/* Produces a fresh frame and returns a borrowed pointer. */
termole_core_status termole_vt_frame(termole_vt* vt, const termole_frame** out_frame);

/* Encode one cell's grapheme cluster as UTF-8. out_len receives the byte count
 * (excluding the NUL). Returns OUT_OF_SPACE with required size when cap is too
 * small. */
termole_core_status termole_cell_text(const termole_cell* cell, char* buffer, size_t capacity,
                                      size_t* out_len);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TERMOLE_CORE_H */
