#pragma once

#include <windows.h>

#include <termole/termole.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct termole_cview termole_cview;

typedef struct termole_cview_options {
    size_t size;
    uint32_t abi_version;
    const wchar_t* font_family;
    int font_size;
    int theme;
} termole_cview_options;

#define TERMOLE_CVIEW_OPTIONS_INIT \
    { sizeof(termole_cview_options), 1u, NULL, 20, 0 }

/* Creates a child window under parent that renders a C session via the C GDI
   renderer. The session must outlive the view. */
termole_status termole_cview_create(HWND parent, termole_session* session,
                                    const termole_cview_options* options,
                                    termole_cview** out_view);
void termole_cview_destroy(termole_cview* view);
HWND termole_cview_hwnd(const termole_cview* view);/* Selects a document range (0 = oldest scrollback row). */
void termole_cview_set_selection(termole_cview* view, int32_t start_row, int32_t start_column,
                                 int32_t end_row, int32_t end_column);
void termole_cview_clear_selection(termole_cview* view);

/* Selected UTF-8 text; library allocated, free with termole_free(). */
termole_status termole_cview_get_selection_text(termole_cview* view, char** out_text,
                                                size_t* out_size);

uint64_t termole_cview_paint_count(const termole_cview* view);

#ifdef __cplusplus
}
#endif
