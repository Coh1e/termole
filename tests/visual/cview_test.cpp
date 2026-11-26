#include <termole/cview.h>

#include "../sdk-consumer/capture.h"
#include "gui/gdi_render.h"
#include "session/viewport.h"

#include <objbase.h>
#include <stdio.h>
#include <string.h>
#include <thread>

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

static LRESULT CALLBACK parent_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, message, wparam, lparam);
}

static void pump(void)
{
    MSG message;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

static bool wait_for_text(termole_session* session, const char* expected)
{
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        pump();
        char* text = NULL;
        if (termole_session_get_screen_text(session, &text, NULL) != TERMOLE_STATUS_OK) return false;
        const bool found = strstr(text, expected) != NULL;
        termole_free(text);
        if (found) return true;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    return false;
}

static void drag(HWND window, int start_x, int start_y, int end_x, int end_y)
{
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start_x, start_y));
    SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(end_x, end_y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(end_x, end_y));
    RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
}

static void check_selection_text(termole_cview* view, const std::string& expected,
                                 const char* description)
{
    char* text = NULL;
    size_t length = 0;
    const auto status = termole_cview_get_selection_text(view, &text, &length);
    const bool matches = status == TERMOLE_STATUS_OK && text && length == expected.size() &&
                         memcmp(text, expected.data(), length) == 0;
    if (!matches) fprintf(stderr, "Expected [%s], got [%s]\n", expected.c_str(), text ? text : "");
    check(matches, description);
    termole_free(text);
}

static bool has_selection_color(COLORREF pixel)
{
    return pixel != CLR_INVALID && GetBValue(pixel) > GetRValue(pixel) + 32;
}

static void check_selection_pixels(HWND window, const char* file, int x, int selected_y,
                                   int outside_y)
{
    capture::Sample samples[] = { { x, selected_y }, { x, outside_y } };
    capture::Stats pixels;
    std::string error;
    const ULONGLONG deadline = GetTickCount64() + 3000;
    // Window composition can still show the preceding paint. Sample the same
    // bitmap we save to disk and keep the final capture if the test fails.
    do {
        pump();
        if (!capture::window(window, file, pixels, error, samples)) break;
        if (has_selection_color(samples[0].color) && samples[1].color != CLR_INVALID &&
            !has_selection_color(samples[1].color)) {
            check(true, "captured highlight is on the selected row and clear on the adjacent row");
            return;
        }
        Sleep(20);
    } while (GetTickCount64() < deadline);
    fprintf(stderr, "%s: selected=%#lx, adjacent=%#lx %s\n",
            file, samples[0].color, samples[1].color, error.c_str());
    check(false, "capture the selected row and its unselected neighbor");
}

static void check_viewport_resizes(termole_session* session)
{
    check(termole_session_resize(session, 48, 8) == TERMOLE_STATUS_OK, "set initial viewport size");
    bool resized = true;
    std::thread writer([&] {
        for (int i = 0; i < 32; ++i) {
            const auto status = (i % 2 == 0) ? termole_session_resize(session, 96, 16)
                                             : termole_session_resize(session, 48, 8);
            if (status != TERMOLE_STATUS_OK) resized = false;
        }
    });
    termole_viewport viewport{};
    bool coherent = true;
    for (int i = 0; i < 32; ++i) {
        if (termole_session_read_viewport(session, 0, &viewport) != TERMOLE_STATUS_OK) {
            coherent = false;
            break;
        }
        const auto& frame = viewport.frame;
        const bool expected_size = (frame.columns == 48 && frame.rows == 8) ||
                                   (frame.columns == 96 && frame.rows == 16);
        if (!expected_size || !frame.cells ||
            static_cast<size_t>(frame.columns) * frame.rows > viewport.capacity ||
            viewport.first_document_row != 0) {
            coherent = false;
            break;
        }
    }
    writer.join();
    termole_free(viewport.frame.cells);
    check(resized && coherent, "resized alternate-screen frames keep their grid and first row");
}

int main(void)
{
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = parent_proc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.lpszClassName = L"TermoleCViewParent";
    RegisterClassExW(&window_class);

    HWND parent = CreateWindowExW(0, L"TermoleCViewParent", L"CView probe", WS_POPUP,
                                  0, 0, 640, 400, NULL, NULL, GetModuleHandleW(NULL), NULL);
    check(parent != NULL, "parent window");

    ShowWindow(parent, SW_SHOWNA);
    UpdateWindow(parent);
    termole_session_options options = TERMOLE_SESSION_OPTIONS_INIT;
    options.program = L"cmd.exe";
    const wchar_t* args[] = { L"/d", L"/q", L"/c",
        L"(for /l %i in (1,1,60) do @echo C-VIEW-ROW-%i) & pause >nul & "
        L"echo \x1b[?1049h\x1b[2J\x1b[2;1HALT-READY & pause >nul" };
    options.arguments = args;
    options.argument_count = 4;
    options.columns = 80;
    options.rows = 24;
    termole_session* session = NULL;
    check(termole_session_create(&options, &session) == TERMOLE_STATUS_OK, "session");

    termole_cview_options view_options = TERMOLE_CVIEW_OPTIONS_INIT;
    termole_cview* view = NULL;
    check(termole_cview_create(parent, session, &view_options, &view) == TERMOLE_STATUS_OK,
          "C view created");
    if (view) {
        HWND window = termole_cview_hwnd(view);
        ShowWindow(window, SW_SHOW);
        check(wait_for_text(session, "C-VIEW-ROW-60"), "child has filled the primary scrollback");
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        check(termole_cview_paint_count(view) > 0, "C view painted");
        HDC dc = GetDC(window);
        int ink = 0;
        if (dc) {
            for (int y = 0; y < 96; y += 3) {
                for (int x = 0; x < 140; x += 3) {
                    const COLORREF pixel = GetPixel(dc, x, y);
                    if (pixel != CLR_INVALID && GetRValue(pixel) > 64) ++ink;
                }
            }
            ReleaseDC(window, dc);
        }
        check(ink > 20, "C view draws text pixels");
        termole_cview_set_selection(view, 0, 0, 23, 79);
        char* selected = NULL;
        size_t selected_size = 0;
        check(termole_cview_get_selection_text(view, &selected, &selected_size) ==
                  TERMOLE_STATUS_OK &&
                  selected && selected_size > 0,
              "C view selection text");
        termole_free(selected);
        termole_gdi_renderer* metrics = termole_gdi_create(view_options.font_family,
                                                           view_options.font_size, view_options.theme);
        const int cell_width = (int)termole_gdi_cell_width(metrics);
        const int cell_height = (int)termole_gdi_cell_height(metrics);
        termole_gdi_destroy(metrics);
        check(cell_width > 0 && cell_height > 0, "measure the configured GDI font");
        const HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        check(SUCCEEDED(com), "initialize the PNG encoder");

        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        uint16_t columns = 0, rows = 0;
        check(termole_session_get_size(session, &columns, &rows) == TERMOLE_STATUS_OK && rows > 1,
              "read the displayed grid size");
        const int last_y = (rows - 1) * cell_height + 2;
        // The captured mouse can travel past the bottom and right of the HWND.
        drag(window, 2, last_y, (columns + 2) * cell_width, (rows + 4) * cell_height);
        std::string bottom_line = "C-VIEW-ROW-58";
        bottom_line.resize(columns, ' ');
        check_selection_text(view, bottom_line, "dragging outside stops at the last visible cell");
        check_selection_pixels(window, "primary-selection.png", (columns - 2) * cell_width,
                               last_y, last_y - cell_height);

        check(termole_session_write(session, "\r", 1) == TERMOLE_STATUS_OK, "switch screens");
        check(wait_for_text(session, "ALT-READY"), "child switched to its alternate screen");
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        drag(window, 2, cell_height + 2, 2 * cell_width + 2, cell_height + 2);
        check_selection_text(view, "ALT", "dragging on the second alternate-screen row selects ALT");
        check_selection_pixels(window, "alternate-selection.png", 2, cell_height + 2, 2);
        if (SUCCEEDED(com)) CoUninitialize();
        termole_cview_clear_selection(view);
        check(termole_cview_get_selection_text(view, &selected, &selected_size) ==
                  TERMOLE_STATUS_INVALID_STATE,
              "cleared C view selection is empty");
        check(termole_cview_hwnd(view) != NULL, "C view has a window");
        check_viewport_resizes(session);
        termole_cview_destroy(view);
    }

    termole_cview_options light_options = TERMOLE_CVIEW_OPTIONS_INIT;
    light_options.font_family = L"Consolas";
    light_options.font_size = 24;
    light_options.theme = 1;
    termole_cview* light_view = NULL;
    check(termole_cview_create(parent, session, &light_options, &light_view) == TERMOLE_STATUS_OK,
          "light C view created");
    if (light_view) {
        RedrawWindow(termole_cview_hwnd(light_view), NULL, NULL,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        HDC dc = GetDC(termole_cview_hwnd(light_view));
        if (dc) {
            const COLORREF corner = GetPixel(dc, 2, 2);
            ReleaseDC(termole_cview_hwnd(light_view), dc);
            check(corner == RGB(250, 250, 250), "C view light theme background");
        }
        termole_cview_destroy(light_view);
    }

    termole_session_destroy(session);
    DestroyWindow(parent);

    if (failures != 0) {
        fprintf(stderr, "%d C view check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: C view\n");
    return 0;
}
