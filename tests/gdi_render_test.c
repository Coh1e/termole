#include "gui/gdi_render.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what)
{
    if (condition) {
        printf("PASS: %s\n", what);
    } else {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

int main(void)
{
    termole_vt* vt = NULL;
    if (termole_vt_create(20, 4, &vt) != TERMOLE_CORE_OK || !vt) {
        fprintf(stderr, "FAIL: create terminal\n");
        return 1;
    }
    const char* text = "Hi C GUI";
    check(termole_vt_feed(vt, text, strlen(text)) == TERMOLE_CORE_OK, "feed text");
    const termole_frame* frame = NULL;
    check(termole_vt_frame(vt, &frame) == TERMOLE_CORE_OK && frame != NULL, "frame");

    termole_gdi_renderer* renderer = termole_gdi_create(L"Consolas", 20, 0);
    check(renderer != NULL, "create GDI renderer");
    if (!renderer) {
        termole_vt_free(vt);
        return 1;
    }
    check(termole_gdi_cell_width(renderer) > 1.0f, "cell width");
    check(termole_gdi_cell_height(renderer) > 1.0f, "cell height");

    HDC screen = GetDC(NULL);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 400, 160);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    RECT clear = { 0, 0, 400, 160 };
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory, &clear, black);
    DeleteObject(black);

    termole_gdi_paint(renderer, memory, frame, 1);

    int ink = 0;
    for (int y = 0; y < 160; y += 2) {
        for (int x = 0; x < 400; x += 2) {
            const COLORREF pixel = GetPixel(memory, x, y);
            if (pixel != RGB(0, 0, 0)) ++ink;
        }
    }
    check(ink > 20, "renderer paints non-background pixels");

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(NULL, screen);
    termole_gdi_destroy(renderer);
    termole_vt_free(vt);

    if (failures != 0) {
        fprintf(stderr, "%d GDI renderer check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: C GDI renderer\n");
    return 0;
}
