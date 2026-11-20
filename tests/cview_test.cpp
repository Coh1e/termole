#include <termole/cview.h>

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
    options.columns = 80;
    options.rows = 24;
    termole_session* session = NULL;
    check(termole_session_create(&options, &session) == TERMOLE_STATUS_OK, "session");

    termole_cview_options view_options = TERMOLE_CVIEW_OPTIONS_INIT;
    termole_cview* view = NULL;
    check(termole_cview_create(parent, session, &view_options, &view) == TERMOLE_STATUS_OK,
          "C view created");
    if (view) {
        ShowWindow(termole_cview_hwnd(view), SW_SHOW);
        (void)termole_session_write(session, "echo C-VIEW-MARKER\r\n", 20);
        InvalidateRect(termole_cview_hwnd(view), NULL, TRUE);
        RedrawWindow(termole_cview_hwnd(view), NULL, NULL,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        for (int index = 0; index < 100; ++index) {
            pump();
            Sleep(20);
        }
        check(termole_cview_paint_count(view) > 0, "C view painted");
        RedrawWindow(termole_cview_hwnd(view), NULL, NULL,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        HDC dc = GetDC(termole_cview_hwnd(view));
        int ink = 0;
        if (dc) {
            for (int y = 0; y < 300; y += 2) {
                for (int x = 0; x < 600; x += 2) {
                    const COLORREF pixel = GetPixel(dc, x, y);
                    if (pixel != RGB(12, 12, 12) && pixel != RGB(0, 0, 0)) ++ink;
                }
            }
            ReleaseDC(termole_cview_hwnd(view), dc);
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
        termole_cview_clear_selection(view);
        check(termole_cview_get_selection_text(view, &selected, &selected_size) ==
                  TERMOLE_STATUS_INVALID_STATE,
              "cleared C view selection is empty");
        check(termole_cview_hwnd(view) != NULL, "C view has a window");
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
