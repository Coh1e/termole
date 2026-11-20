#include "main.h"
#include <termole/cview.h>

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static termole_session* g_session;
static termole_cview* g_view;

static void env_text(const wchar_t* name, wchar_t* out, size_t capacity)
{
    if (!capacity) return;
    out[0] = L'\0';
    const DWORD length = GetEnvironmentVariableW(name, out, (DWORD)capacity);
    if (length == 0 || length >= capacity) out[0] = L'\0';
}

static int env_int(const wchar_t* name, int fallback)
{
    wchar_t buffer[32];
    env_text(name, buffer, sizeof(buffer) / sizeof(buffer[0]));
    return buffer[0] ? (int)wcstol(buffer, NULL, 10) : fallback;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_SIZE:
        if (g_view) {
            RECT client;
            GetClientRect(window, &client);
            MoveWindow(termole_cview_hwnd(g_view), 0, 0, client.right, client.bottom, TRUE);
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_TIMER:
        if (wparam == 1) DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

int termole_cgui_run(HINSTANCE instance, const termole_cgui_options* options, int show)
{
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    window_class.lpszClassName = L"TermoleCGui";
    if (!RegisterClassExW(&window_class)) return 1;

    wchar_t title[128];
    env_text(L"TERMOLE_CGUI_TITLE", title, sizeof(title) / sizeof(title[0]));
    if (!title[0]) wcscpy(title, L"Termole (C GUI)");
    int width = options->width ? options->width : env_int(L"TERMOLE_CGUI_WIDTH", 1000);
    int height = options->height ? options->height : env_int(L"TERMOLE_CGUI_HEIGHT", 640);
    if (width < 200) width = 200;
    if (height < 120) height = 120;
    HWND window = CreateWindowExW(0, L"TermoleCGui", options->title ? options->title : title,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, width, height, NULL, NULL, instance, NULL);
    if (!window) return 1;
    termole_session_options session_options = options->session;
    if (!session_options.columns)
        session_options.columns = (uint16_t)env_int(L"TERMOLE_CGUI_COLUMNS", 80);
    if (!session_options.rows)
        session_options.rows = (uint16_t)env_int(L"TERMOLE_CGUI_ROWS", 24);
    if (termole_session_create(&session_options, &g_session) != TERMOLE_STATUS_OK) {
        MessageBoxW(window, L"Could not start the session.", L"Termole", MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }
    termole_cview_options view_options = TERMOLE_CVIEW_OPTIONS_INIT;
    wchar_t font[64];
    env_text(L"TERMOLE_CGUI_FONT", font, sizeof(font) / sizeof(font[0]));
    view_options.font_family = options->font ? options->font : (font[0] ? font : L"Consolas");
    const int fallback_size = env_int(L"TERMOLE_CGUI_FONT_SIZE", 20);
    view_options.font_size = options->font_size ? options->font_size :
        (uint32_t)(fallback_size >= 8 && fallback_size <= 96 ? fallback_size : 20);
    wchar_t theme[16];
    env_text(L"TERMOLE_CGUI_THEME", theme, sizeof(theme) / sizeof(theme[0]));
    view_options.theme = options->theme ? options->theme == TERMOLE_THEME_LIGHT :
        wcscmp(theme, L"light") == 0;
    if (termole_cview_create(window, g_session, &view_options, &g_view) != TERMOLE_STATUS_OK) {
        MessageBoxW(window, L"Could not create the C view.", L"Termole", MB_ICONERROR);
        termole_session_destroy(g_session);
        DestroyWindow(window);
        return 1;
    }

    RECT client;
    GetClientRect(window, &client);
    MoveWindow(termole_cview_hwnd(g_view), 0, 0, client.right, client.bottom, TRUE);
    ShowWindow(window, show);
    if (options->smoke) SetTimer(window, 1, 1200, NULL);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    termole_cview_destroy(g_view);
    g_view = NULL;
    termole_session_destroy(g_session);
    g_session = NULL;
    return 0;
}

#ifndef TERMOLE_CGUI_NO_ENTRY
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    const termole_cgui_options options = {
        .session = TERMOLE_SESSION_OPTIONS_INIT,
        .smoke = true,
    };
    return termole_cgui_run(instance, &options, show);
}
#endif
