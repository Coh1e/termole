#include "gui/gdi_render.h"

#include <stdlib.h>
#include <string.h>

struct termole_gdi_renderer {
    HFONT normal;
    HFONT bold;
    HFONT italic;
    HFONT bold_italic;
    float cell_width;
    float cell_height;
    uint32_t default_foreground;
    uint32_t default_background;
};

static COLORREF color_of(uint32_t rgb)
{
    return RGB((BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb);
}

static HFONT make_font(const wchar_t* family, int size, int weight, BOOL italic)
{
    return CreateFontW(-size, 0, 0, 0, weight, italic, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       FIXED_PITCH | FF_MODERN, family);
}

termole_gdi_renderer* termole_gdi_create(const wchar_t* font_family, int font_size, int theme)
{
    if (font_size < 6) font_size = 6;
    if (font_size > 96) font_size = 96;
    const wchar_t* family = (font_family && *font_family) ? font_family : L"Consolas";

    termole_gdi_renderer* renderer = calloc(1, sizeof(*renderer));
    if (!renderer) return NULL;

    renderer->normal = make_font(family, font_size, FW_NORMAL, FALSE);
    renderer->bold = make_font(family, font_size, FW_BOLD, FALSE);
    renderer->italic = make_font(family, font_size, FW_NORMAL, TRUE);
    renderer->bold_italic = make_font(family, font_size, FW_BOLD, TRUE);
    if (!renderer->normal || !renderer->bold || !renderer->italic || !renderer->bold_italic) {
        termole_gdi_destroy(renderer);
        return NULL;
    }

    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) {
        termole_gdi_destroy(renderer);
        return NULL;
    }
    HGDIOBJ previous = SelectObject(dc, renderer->normal);
    TEXTMETRICW metrics;
    GetTextMetricsW(dc, &metrics);
    SIZE extent = { 0, 0 };
    GetTextExtentPoint32W(dc, L"M", 1, &extent);
    renderer->cell_width = (float)(extent.cx > 0 ? extent.cx : metrics.tmAveCharWidth);
    renderer->cell_height = (float)(metrics.tmHeight + metrics.tmExternalLeading);
    SelectObject(dc, previous);
    DeleteDC(dc);

    if (renderer->cell_width <= 0.0f || renderer->cell_height <= 0.0f) {
        termole_gdi_destroy(renderer);
        return NULL;
    }

    if (theme == 1) {
        renderer->default_foreground = 0x001E1E1Eu;
        renderer->default_background = 0x00FAFAFAu;
    } else {
        renderer->default_foreground = 0x00CCCCCCu;
        renderer->default_background = 0x000C0C0Cu;
    }
    return renderer;
}

void termole_gdi_destroy(termole_gdi_renderer* renderer)
{
    if (!renderer) return;
    if (renderer->normal) DeleteObject(renderer->normal);
    if (renderer->bold) DeleteObject(renderer->bold);
    if (renderer->italic) DeleteObject(renderer->italic);
    if (renderer->bold_italic) DeleteObject(renderer->bold_italic);
    free(renderer);
}

float termole_gdi_cell_width(const termole_gdi_renderer* renderer)
{
    return renderer ? renderer->cell_width : 0.0f;
}

float termole_gdi_cell_height(const termole_gdi_renderer* renderer)
{
    return renderer ? renderer->cell_height : 0.0f;
}

static int cell_foreground(const termole_gdi_renderer* renderer, const termole_cell* cell)
{
    int color = cell->foreground_is_default ? (int)renderer->default_foreground
                                            : (int)cell->foreground;
    int background = cell->background_is_default ? (int)renderer->default_background
                                                 : (int)cell->background;
    if ((cell->attributes & TERMOLE_CELL_INVERSE) != 0) {
        const int swap = color;
        color = background;
        background = swap;
    }
    return color;
}

static int cell_background(const termole_gdi_renderer* renderer, const termole_cell* cell)
{
    int background = cell->background_is_default ? (int)renderer->default_background
                                                 : (int)cell->background;
    if ((cell->attributes & TERMOLE_CELL_INVERSE) != 0) {
        background = cell->foreground_is_default ? (int)renderer->default_foreground
                                                 : (int)cell->foreground;
    }
    return background;
}

static void append_codepoint(WCHAR* out, int* length, uint32_t codepoint)
{
    if (codepoint > 0x10FFFF) codepoint = 0xFFFD;
    if (codepoint >= 0x10000) {
        codepoint -= 0x10000;
        out[(*length)++] = (WCHAR)(0xD800 + (codepoint >> 10));
        out[(*length)++] = (WCHAR)(0xDC00 + (codepoint & 0x3FF));
    } else {
        out[(*length)++] = (WCHAR)codepoint;
    }
}

void termole_gdi_paint(termole_gdi_renderer* renderer, HDC hdc, const termole_frame* frame,
                       int focused)
{
    if (!renderer || !hdc || !frame || !frame->cells) return;

    RECT full = { 0, 0, (LONG)(frame->columns * renderer->cell_width),
                  (LONG)(frame->rows * renderer->cell_height) };
    HBRUSH background = CreateSolidBrush(color_of(renderer->default_background));
    FillRect(hdc, &full, background);
    DeleteObject(background);
    SetBkMode(hdc, TRANSPARENT);

    for (uint16_t row = 0; row < frame->rows; ++row) {
        const int top = (int)(row * renderer->cell_height);
        for (uint16_t column = 0; column < frame->columns; ++column) {
            const termole_cell* cell = &frame->cells[(size_t)row * frame->columns + column];
            if (cell->continuation) continue;
            const int left = (int)(column * renderer->cell_width);
            const int right = (int)((column + (cell->width > 2 ? 2 : cell->width)) *
                                    renderer->cell_width);
            const int background_rgb = cell->selected ? 0x00264F78
                                                : cell_background(renderer, cell);
            if (background_rgb != (int)renderer->default_background) {
                RECT box = { left, top, right, top + (LONG)renderer->cell_height };
                HBRUSH brush = CreateSolidBrush(color_of((uint32_t)background_rgb));
                FillRect(hdc, &box, brush);
                DeleteObject(brush);
            }

            if ((cell->attributes & TERMOLE_CELL_INVISIBLE) != 0) continue;
            WCHAR text[TERMOLE_CELL_MAX_CODEPOINTS * 2 + 1];
            int length = 0;
            for (uint8_t index = 0; index < cell->codepoint_count; ++index)
                append_codepoint(text, &length, cell->codepoints[index]);
            text[length] = L'\0';
            if (length == 0) continue;

            SetTextColor(hdc, color_of((uint32_t)cell_foreground(renderer, cell)));
            HFONT font = renderer->normal;
            if ((cell->attributes & TERMOLE_CELL_BOLD) != 0 &&
                (cell->attributes & TERMOLE_CELL_ITALIC) != 0) {
                font = renderer->bold_italic;
            } else if ((cell->attributes & TERMOLE_CELL_BOLD) != 0) {
                font = renderer->bold;
            } else if ((cell->attributes & TERMOLE_CELL_ITALIC) != 0) {
                font = renderer->italic;
            }
            HGDIOBJ previous = SelectObject(hdc, font);
            ExtTextOutW(hdc, left, top, 0, NULL, text, (UINT)length, NULL);
            if ((cell->attributes & TERMOLE_CELL_UNDERLINE) != 0) {
                const int y = top + (int)(renderer->cell_height * 0.84f);
                HPEN pen = CreatePen(PS_SOLID, 1, color_of((uint32_t)cell_foreground(renderer, cell)));
                HGDIOBJ old_pen = SelectObject(hdc, pen);
                MoveToEx(hdc, left, y, NULL);
                LineTo(hdc, right, y);
                SelectObject(hdc, old_pen);
                DeleteObject(pen);
            }
            SelectObject(hdc, previous);
        }
    }

    if (focused && frame->cursor_visible && frame->cursor_row < frame->rows &&
        frame->cursor_column < frame->columns) {
        RECT cursor = {
            (LONG)(frame->cursor_column * renderer->cell_width),
            (LONG)(frame->cursor_row * renderer->cell_height),
            (LONG)((frame->cursor_column + 1) * renderer->cell_width),
            (LONG)((frame->cursor_row + 1) * renderer->cell_height),
        };
        HPEN pen = CreatePen(PS_SOLID, 2, color_of(renderer->default_foreground));
        HGDIOBJ old_pen = SelectObject(hdc, pen);
        HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, cursor.left, cursor.top, cursor.right, cursor.bottom);
        SelectObject(hdc, old_brush);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);
    }
}
