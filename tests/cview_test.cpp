#include <termole/cview.h>

#include "sdk-consumer/capture.h"
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
            viewport.first_document_row + frame.rows != viewport.document_rows) {
            coherent = false;
            break;
        }
    }
    writer.join();
    termole_free(viewport.frame.cells);
    check(resized && coherent, "copied frames match their grid and document rows during resize");
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
        L"echo \x1b[?1049hALT-READY & pause >nul" };
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
        ShowWindow(termole_cview_hwnd(view), SW_SHOW);
        check(wait_for_text(session, "C-VIEW-ROW-60"), "child has filled the primary scrollback");
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
        // The alternate screen has no history. A previous scroll offset must
        // not move the View's selected document row outside that new frame.
        HWND window = termole_cview_hwnd(view);
        SendMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
        check(termole_session_write(session, "\r", 1) == TERMOLE_STATUS_OK, "switch screens");
        check(wait_for_text(session, "ALT-READY"), "child switched to its alternate screen");
        termole_cview_set_selection(view, 0, 0, 0, 4);
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        const HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        capture::Stats pixels;
        std::string error;
        check(SUCCEEDED(com) && capture::window(window, "cview-selection.png", pixels, error),
              "capture the selected alternate-screen row");
        if (!error.empty()) fprintf(stderr, "%s\n", error.c_str());
        if (SUCCEEDED(com)) CoUninitialize();
        dc = GetDC(window);
        if (dc) {
            const COLORREF top_cell = GetPixel(dc, 2, 2);
            ReleaseDC(window, dc);
            check(top_cell != CLR_INVALID && GetBValue(top_cell) > GetRValue(top_cell) + 32,
                  "selection highlights the first alternate-screen row");
        } else {
            check(false, "obtain the C view pixels");
        }
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
