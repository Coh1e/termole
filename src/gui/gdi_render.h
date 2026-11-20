#pragma once

#include <windows.h>

#include <termole/core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct termole_gdi_renderer termole_gdi_renderer;

/* Theme 0 = dark, 1 = light. Returns NULL on allocation or GDI failure. */
termole_gdi_renderer* termole_gdi_create(const wchar_t* font_family, int font_size, int theme);
void termole_gdi_destroy(termole_gdi_renderer* renderer);

float termole_gdi_cell_width(const termole_gdi_renderer* renderer);
float termole_gdi_cell_height(const termole_gdi_renderer* renderer);

/* Paints a full frame into an HDC. Coordinates start at (0,0). */
void termole_gdi_paint(termole_gdi_renderer* renderer, HDC hdc, const termole_frame* frame,
                       int focused);

#ifdef __cplusplus
}
#endif
