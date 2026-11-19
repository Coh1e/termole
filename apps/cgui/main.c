#include <termole/cview.h>
#include <termole/termole.h>

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static termole_session* g_session;
static termole_cview* g_view;

static void env_text(const wchar_t* name, wchar_t* out, size_t capacity)
{
    out[0] = L'\0';
    if (!capacity) return;
    const DWORD length = GetEnvironmentVariableW(name, out, (DWORD)capacity);
    if (length == 0 || length >= capacity) out[0] = L'\0';
}

static int env_int(const wchar_t* name, int fallback)
{
    wchar_t buffer[32];
    env_text(name, buffer, sizeof(buffer) / sizeof(buffer[0]));
    return buffer[0] ? (int)wcstol(buffer, NULL, 10) : fallback;
}
static const wchar_t* option_source(const wchar_t* command_line, const wchar_t* name)
{
    wchar_t base[64];
    size_t length = 0;
    while (name[length] && name[length] != L'=' &&
           length + 1 < sizeof(base) / sizeof(base[0])) {
        base[length] = name[length];
        ++length;
    }
    base[length] = L'\0';
    wchar_t quoted[72];
    if (length + 3 < sizeof(quoted) / sizeof(quoted[0])) {
        quoted[0] = L'"';
        memcpy(quoted + 1, base, length * sizeof(wchar_t));
        quoted[length + 1] = L'"';
        quoted[length + 2] = L'\0';
        const wchar_t* quoted_found = wcsstr(command_line, quoted);
        if (quoted_found) {
            const wchar_t* source = quoted_found + length + 2;
            while (*source == L' ') ++source;
            return source;
        }
    }
    const wchar_t* found = wcsstr(command_line, base);
    if (!found) return NULL;
    const wchar_t* source = found + length;
    while (*source == L'=' || *source == L' ') ++source;
    return source;
}

static int option_int(const wchar_t* command_line, const wchar_t* name, int fallback)
{
    const wchar_t* source = option_source(command_line, name);
    if (!source) return fallback;
    return (int)wcstol(source, NULL, 10);
}

static void option_text(const wchar_t* command_line, const wchar_t* name, wchar_t* out,
                        size_t capacity)
{
    out[0] = L'\0';
    if (capacity == 0) return;
    const wchar_t* source = option_source(command_line, name);
    if (!source) return;
    size_t length = 0;
    if (*source == L'"') {
        ++source;
        while (*source && *source != L'"' && length + 1 < capacity)
            out[length++] = *source++;
    } else {
        while (*source && *source != L' ' && length + 1 < capacity)
            out[length++] = *source++;
    }
    out[length] = L'\0';
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

int termole_cgui_run(HINSTANCE instance, PWSTR command_line, int show)
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
    option_text(command_line, L"--title=", title, sizeof(title) / sizeof(title[0]));
    if (!title[0]) env_text(L"TERMOLE_CGUI_TITLE", title, sizeof(title) / sizeof(title[0]));
    if (!title[0]) wcscpy(title, L"Termole (C GUI)");
    int width = option_int(command_line, L"--width=", env_int(L"TERMOLE_CGUI_WIDTH", 1000));
    int height = option_int(command_line, L"--height=", env_int(L"TERMOLE_CGUI_HEIGHT", 640));
    if (width < 200) width = 200;
    if (height < 120) height = 120;
    HWND window = CreateWindowExW(0, L"TermoleCGui", title,
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, width, height, NULL, NULL, instance, NULL);
    if (!window) return 1;
    wchar_t program[512];
    option_text(command_line, L"--program=", program, sizeof(program) / sizeof(program[0]));
    wchar_t argument_storage[2048];
    const wchar_t* argument_ptrs[32];
    int argument_count = 0;
    argument_storage[0] = L'\0';
    const wchar_t* argument_tail = wcsstr(command_line, L"-- ");
    if (argument_tail) argument_tail += 3;
    else {
        argument_tail = wcsstr(command_line, L" -e ");
        if (argument_tail) argument_tail += 4;
        else {
            argument_tail = wcsstr(command_line, L" --stdio ");
            if (argument_tail) argument_tail += 9;
            else {
                argument_tail = wcsstr(command_line, L"\"-e\"");
                if (argument_tail) argument_tail += 4;
                else {
                    argument_tail = wcsstr(command_line, L"\"--stdio\"");
                    if (argument_tail) argument_tail += 9;
                }
            }
        }
    }
    if (argument_tail) {
        wcscpy(argument_storage, argument_tail);
        wchar_t* cursor = argument_storage;
        while (*cursor && argument_count < 32) {
            while (*cursor == L' ') ++cursor;
            if (!*cursor) break;
            if (*cursor == L'"') ++cursor;
            argument_ptrs[argument_count++] = cursor;
            while (*cursor && *cursor != L' ' && *cursor != L'"') ++cursor;
            if (*cursor) *cursor++ = L'\0';
        }
    }
    if (!program[0] && argument_count > 0) {
        wcscpy(program, argument_ptrs[0]);
        for (int index = 1; index < argument_count; ++index)
            argument_ptrs[index - 1] = argument_ptrs[index];
        --argument_count;
    }
    termole_session_options session_options = TERMOLE_SESSION_OPTIONS_INIT;
    if (program[0]) session_options.program = program;
    session_options.arguments = argument_count ? argument_ptrs : NULL;
    session_options.argument_count = (uint32_t)argument_count;
    session_options.columns = (uint16_t)option_int(command_line, L"--columns=",
                                                    env_int(L"TERMOLE_CGUI_COLUMNS", 80));
    session_options.rows = (uint16_t)option_int(command_line, L"--rows=",
                                                env_int(L"TERMOLE_CGUI_ROWS", 24));
    if (termole_session_create(&session_options, &g_session) != TERMOLE_STATUS_OK) {
        MessageBoxW(window, L"Could not start the session.", L"Termole", MB_ICONERROR);
        DestroyWindow(window);
        return 1;
    }
    termole_cview_options view_options = TERMOLE_CVIEW_OPTIONS_INIT;
    wchar_t font[64];
    option_text(command_line, L"--font=", font, sizeof(font) / sizeof(font[0]));
    if (!font[0]) env_text(L"TERMOLE_CGUI_FONT", font, sizeof(font) / sizeof(font[0]));
    view_options.font_family = font[0] ? font : L"Consolas";
    view_options.font_size = option_int(command_line, L"--font-size=", env_int(L"TERMOLE_CGUI_FONT_SIZE", 20));
    wchar_t theme[16];
    env_text(L"TERMOLE_CGUI_THEME", theme, sizeof(theme) / sizeof(theme[0]));
    view_options.theme = wcsstr(command_line, L"--theme=light") || wcscmp(theme, L"light") == 0 ? 1 : 0;
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
    if (command_line && wcsstr(command_line, L"--smoke")) SetTimer(window, 1, 1200, NULL);
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
    return termole_cgui_run(instance, command_line, show);
}
#endif
