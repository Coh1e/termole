#include <termole/cview.h>
#include "core/utf8_encode.h"
#include "core/input.h"

#include "gui/gdi_render.h"
#include "session/viewport.h"
#include <windowsx.h>
#include <imm.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TERMOLE_CVIEW_CLASS L"TermoleViewWindow"
#define TERMOLE_CVIEW_TIMER 1
#define TERMOLE_CVIEW_MIN_COLUMNS 1
#define TERMOLE_CVIEW_MAX_COLUMNS 1000
#define TERMOLE_CVIEW_MIN_ROWS 1
#define TERMOLE_CVIEW_MAX_ROWS 1000

struct termole_cview {
    HWND window;
    termole_session* session;
    termole_gdi_renderer* renderer;
    termole_viewport viewport;
    uint16_t columns;
    uint16_t rows;
    int scroll_offset;
    int selection_active;
    int selection_drag;
    int mouse_reporting;
    int shift_down;
    DWORD last_resize_tick;
    int selection_anchor_row;
    int selection_anchor_column;
    int selection_focus_row;
    int selection_focus_column;
    uint64_t paint_count;
};

static void send_codepoint(termole_cview* view, uint32_t codepoint)
{
    if (codepoint > 0x10FFFF) codepoint = 0xFFFD;
    char bytes[4];
    const size_t length = termole_utf8_encode(codepoint, bytes);
    (void)termole_session_write(view->session, bytes, length);
}

static void send_key(termole_cview* view, WPARAM key)
{
    termole_key translated = 0;
    switch (key) {
    case VK_RETURN: translated = TERMOLE_KEY_ENTER; break;
    case VK_TAB: translated = TERMOLE_KEY_TAB; break;
    case VK_BACK: translated = TERMOLE_KEY_BACKSPACE; break;
    case VK_ESCAPE: translated = TERMOLE_KEY_ESCAPE; break;
    case VK_UP: translated = TERMOLE_KEY_ARROW_UP; break;
    case VK_DOWN: translated = TERMOLE_KEY_ARROW_DOWN; break;
    case VK_LEFT: translated = TERMOLE_KEY_ARROW_LEFT; break;
    case VK_RIGHT: translated = TERMOLE_KEY_ARROW_RIGHT; break;
    case VK_HOME: translated = TERMOLE_KEY_HOME; break;
    case VK_END: translated = TERMOLE_KEY_END; break;
    case VK_DELETE: translated = TERMOLE_KEY_DELETE; break;
    case VK_INSERT: translated = TERMOLE_KEY_INSERT; break;
    case VK_PRIOR: translated = TERMOLE_KEY_PAGE_UP; break;
    case VK_NEXT: translated = TERMOLE_KEY_PAGE_DOWN; break;
    default: return;
    }
    (void)termole_session_send_key(view->session, translated);
}

static void position_ime(termole_cview* view)
{
    HIMC context = ImmGetContext(view->window);
    if (!context) return;
    COMPOSITIONFORM form;
    memset(&form, 0, sizeof(form));
    form.dwStyle = CFS_POINT;
    form.ptCurrentPos.x = 0;
    form.ptCurrentPos.y = (LONG)(view->rows > 0 ? view->rows * termole_gdi_cell_height(view->renderer) : 0);
    ImmSetCompositionWindow(context, &form);
    ImmReleaseContext(view->window, context);
}

static void commit_ime(termole_cview* view, LPARAM flags)
{
    if ((flags & GCS_RESULTSTR) == 0) return;
    HIMC context = ImmGetContext(view->window);
    if (!context) return;
    const LONG bytes = ImmGetCompositionStringW(context, GCS_RESULTSTR, NULL, 0);
    if (bytes > 0) {
        wchar_t* text = (wchar_t*)malloc((size_t)bytes + sizeof(wchar_t));
        if (text) {
            ImmGetCompositionStringW(context, GCS_RESULTSTR, text, bytes);
            text[bytes / (LONG)sizeof(wchar_t)] = L'\0';
            const int utf8_bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
            if (utf8_bytes > 1) {
                char* utf8 = (char*)malloc((size_t)utf8_bytes);
                if (utf8) {
                    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, utf8_bytes, NULL, NULL);
                    (void)termole_session_write(view->session, utf8, (size_t)utf8_bytes - 1);
                    free(utf8);
                }
            }
            free(text);
        }
    }
    ImmReleaseContext(view->window, context);
}
static void paste_clipboard(termole_cview* view)
{
    if (!OpenClipboard(view->window)) return;
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const wchar_t* text = (const wchar_t*)GlobalLock(data);
        if (text) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
            if (bytes > 1) {
                termole_modes modes;
                const int bracketed =
                    termole_session_get_modes(view->session, &modes) == TERMOLE_STATUS_OK &&
                    modes.bracketed_paste != 0;
                char* utf8 = (char*)malloc((size_t)bytes + (bracketed ? TERMOLE_PASTE_OVERHEAD : 0));
                if (utf8) {
                    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, bytes, NULL, NULL)) {
                        size_t length = (size_t)bytes - 1;
                        if (bracketed) {
                            termole_bracket_paste(utf8, length);
                            length += TERMOLE_PASTE_OVERHEAD;
                        }
                        (void)termole_session_write(view->session, utf8, length);
                    }
                    free(utf8);
                }
            }
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
}
static int send_mouse(termole_cview* view, int button, int release)
{
    termole_modes modes;
    if (termole_session_get_modes(view->session, &modes) != TERMOLE_STATUS_OK) return 0;
    if (modes.mouse_tracking == 0) return 0;

    POINT point;
    if (!GetCursorPos(&point) || !ScreenToClient(view->window, &point)) return 0;
    int column = (int)(point.x / termole_gdi_cell_width(view->renderer)) + 1;
    int row = (int)(point.y / termole_gdi_cell_height(view->renderer)) + 1;
    if (column < 1) column = 1;
    if (row < 1) row = 1;
    if (column > view->columns) column = view->columns;
    if (row > view->rows) row = view->rows;

    int code = button;
    if (GetKeyState(VK_SHIFT) & 0x8000) code |= 4;
    if (GetKeyState(VK_MENU) & 0x8000) code |= 8;
    if (GetKeyState(VK_CONTROL) & 0x8000) code |= 16;

    char bytes[64];
    int length = 0;
    if (modes.mouse_sgr) {
        length = snprintf(bytes, sizeof(bytes), "\x1b[<%d;%d;%d%c", code, column, row,
                          release ? 'm' : 'M');
    } else {
        if (code > 63 || column > 223 || row > 223) return 0;
        bytes[0] = 0x1b;
        bytes[1] = '[';
        bytes[2] = 'M';
        bytes[3] = (char)(32 + code);
        bytes[4] = (char)(32 + column);
        bytes[5] = (char)(32 + row);
        length = 6;
    }
    if (length <= 0) return 0;
    (void)termole_session_write(view->session, bytes, (size_t)length);
    return 1;
}
static int cell_at_pixel(int pixel, float cell_size, uint16_t count)
{
    if (pixel <= 0 || !count) return 0;
    const int cell = (int)(pixel / cell_size);
    return cell < count ? cell : count - 1;
}

static void document_cell_at(termole_cview* view, LPARAM point, int* out_row, int* out_column)
{
    const termole_frame* frame = &view->viewport.frame;
    const int row = cell_at_pixel(GET_Y_LPARAM(point), termole_gdi_cell_height(view->renderer),
                                  frame->rows);
    *out_row = view->viewport.first_document_row + row;
    *out_column = cell_at_pixel(GET_X_LPARAM(point), termole_gdi_cell_width(view->renderer),
                                frame->columns);
}

static void copy_selection(termole_cview* view)
{
    if (!view->selection_active) return;
    char* text = NULL;
    size_t size = 0;
    if (termole_session_get_selection_text(view->session, view->selection_anchor_row,
                                           view->selection_anchor_column, view->selection_focus_row,
                                           view->selection_focus_column, &text,
                                           &size) != TERMOLE_STATUS_OK)
        return;
    const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text, (int)size, NULL, 0);
    if (wide_length > 0 && OpenClipboard(view->window)) {
        EmptyClipboard();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (size_t)(wide_length + 1) * sizeof(wchar_t));
        if (memory) {
            wchar_t* target = (wchar_t*)GlobalLock(memory);
            if (target) {
                MultiByteToWideChar(CP_UTF8, 0, text, (int)size, target, wide_length);
                target[wide_length] = L'\0';
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }
    termole_free(text);
}
static void resize_grid(termole_cview* view, int width, int height)
{
    if (!view->renderer || width <= 0 || height <= 0) return;
    int columns = (int)(width / termole_gdi_cell_width(view->renderer));
    int rows = (int)(height / termole_gdi_cell_height(view->renderer));
    if (columns < TERMOLE_CVIEW_MIN_COLUMNS) columns = TERMOLE_CVIEW_MIN_COLUMNS;
    if (columns > TERMOLE_CVIEW_MAX_COLUMNS) columns = TERMOLE_CVIEW_MAX_COLUMNS;
    if (rows < TERMOLE_CVIEW_MIN_ROWS) rows = TERMOLE_CVIEW_MIN_ROWS;
    if (rows > TERMOLE_CVIEW_MAX_ROWS) rows = TERMOLE_CVIEW_MAX_ROWS;
    if (columns == view->columns && rows == view->rows) return;
    const DWORD now = GetTickCount();
    if (view->last_resize_tick != 0 && now - view->last_resize_tick < 80) return;
    view->last_resize_tick = now;
    view->columns = (uint16_t)columns;
    view->rows = (uint16_t)rows;
    (void)termole_session_resize(view->session, view->columns, view->rows);
}

static void paint(termole_cview* view, HDC hdc)
{
    RECT client;
    if (!GetClientRect(view->window, &client)) return;
    resize_grid(view, client.right - client.left, client.bottom - client.top);

    if (termole_session_read_viewport(view->session, view->scroll_offset, &view->viewport) !=
        TERMOLE_STATUS_OK)
        return;

    termole_frame* frame = &view->viewport.frame;
    if (view->selection_active) {
        const bool forward = view->selection_anchor_row < view->selection_focus_row ||
            (view->selection_anchor_row == view->selection_focus_row &&
             view->selection_anchor_column <= view->selection_focus_column);
        const int first_row = forward ? view->selection_anchor_row : view->selection_focus_row;
        const int last_row = forward ? view->selection_focus_row : view->selection_anchor_row;
        const int first_column = forward ? view->selection_anchor_column : view->selection_focus_column;
        const int last_column = forward ? view->selection_focus_column : view->selection_anchor_column;
        for (uint16_t row = 0; row < frame->rows; ++row) {
            const int document_row = view->viewport.first_document_row + row;
            if (document_row < first_row || document_row > last_row) continue;
            const int begin = document_row == first_row ? first_column : 0;
            const int end = document_row == last_row ? last_column : frame->columns - 1;
            for (uint16_t column = 0; column < frame->columns; ++column) {
                if ((int)column < begin || (int)column > end) continue;
                frame->cells[(size_t)row * frame->columns + column].selected = 1;
            }
        }
    }
    termole_gdi_paint(view->renderer, hdc, frame, 1);
    ++view->paint_count;
}

static LRESULT CALLBACK view_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    termole_cview* view = (termole_cview*)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW* create = (CREATESTRUCTW*)lparam;
        view = (termole_cview*)create->lpCreateParams;
        SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)view);
        if (view) view->window = window;
        return TRUE;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (view) InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (view && view->session) {
            termole_event event = TERMOLE_EVENT_INIT;
            while (termole_session_poll_event(view->session, &event) == TERMOLE_STATUS_OK) {
                termole_free(event.text);
                event.text = NULL;
            }
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT state;
        HDC hdc = BeginPaint(window, &state);
        if (view) paint(view, hdc);
        EndPaint(window, &state);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetFocus(window);
        if (view) {
            const bool shift = view->shift_down || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!shift && send_mouse(view, 0, 0)) {
                view->mouse_reporting = 1;
                SetCapture(window);
                return 0;
            }
            document_cell_at(view, lparam, &view->selection_anchor_row,
                             &view->selection_anchor_column);
            view->selection_focus_row = view->selection_anchor_row;
            view->selection_focus_column = view->selection_anchor_column;
            view->selection_active = 1;
            view->selection_drag = 1;
            SetCapture(window);
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (view) {
            if (view->mouse_reporting) {
                if ((wparam & MK_LBUTTON) != 0) (void)send_mouse(view, 32, 0);
            } else if (view->selection_drag && (wparam & MK_LBUTTON) != 0) {
                document_cell_at(view, lparam, &view->selection_focus_row,
                                 &view->selection_focus_column);
                InvalidateRect(window, NULL, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (view) {
            if (view->mouse_reporting) {
                (void)send_mouse(view, 0, 1);
                view->mouse_reporting = 0;
            } else if (view->selection_drag) {
                document_cell_at(view, lparam, &view->selection_focus_row,
                                 &view->selection_focus_column);
                view->selection_drag = 0;
                InvalidateRect(window, NULL, FALSE);
            }
            ReleaseCapture();
        }
        return 0;    case WM_MOUSEWHEEL:
        if (view) {
            if (send_mouse(view, GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 64 : 65, 0)) return 0;
            size_t history = 0;
            (void)termole_session_get_scrollback_line_count(view->session, &history);
            const int steps = GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
            view->scroll_offset += steps * 3;
            if (view->scroll_offset < 0) view->scroll_offset = 0;
            if ((size_t)view->scroll_offset > history) view->scroll_offset = (int)history;
            InvalidateRect(window, NULL, FALSE);
        }
        return 0;    case WM_KEYUP:
        if (view && wparam == VK_SHIFT) view->shift_down = 0;
        return 0;
    case WM_KEYDOWN:
        if (view) {
            if (wparam == VK_SHIFT) view->shift_down = 1;
            if (wparam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                paste_clipboard(view);
                return 0;
            }
            if (wparam == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
                paste_clipboard(view);
                return 0;
            }
            if (wparam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                if (view->selection_active) copy_selection(view);
                else (void)termole_session_send_key(view->session, TERMOLE_KEY_CTRL_C);
                return 0;
            }
            send_key(view, wparam);
        }
        return 0;
    case WM_IME_STARTCOMPOSITION:
        if (view) position_ime(view);
        return 0;
    case WM_IME_COMPOSITION:
        if (view) {
            commit_ime(view, lparam);
            position_ime(view);
        }
        return 0;
    case WM_IME_ENDCOMPOSITION:
        return 0;    case WM_CHAR:
        if (view) {
            if (wparam == 0x03) {
                if (view->selection_active) copy_selection(view);
                else (void)termole_session_send_key(view->session, TERMOLE_KEY_CTRL_C);
            } else if (wparam == 0x16) {
                paste_clipboard(view);
            } else if (wparam == 0x0D) {
                (void)termole_session_send_key(view->session, TERMOLE_KEY_ENTER);
            } else if (wparam == 0x08) {
                (void)termole_session_send_key(view->session, TERMOLE_KEY_BACKSPACE);
            } else if (wparam >= 0x20 && wparam != 0x7F) {
                send_codepoint(view, (uint32_t)wparam);
            }
        }
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

termole_status termole_cview_create(HWND parent, termole_session* session,
                                    const termole_cview_options* options,
                                    termole_cview** out_view)
{
    if (!parent || !session || !out_view) return TERMOLE_STATUS_INVALID_ARGUMENT;
    *out_view = NULL;
    if (options && (options->size < sizeof(termole_cview_options) || options->abi_version != 1u))
        return TERMOLE_STATUS_ABI_MISMATCH;

    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = view_proc;
    window_class.hInstance = GetModuleHandleW(NULL);
    window_class.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    window_class.lpszClassName = TERMOLE_CVIEW_CLASS;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return TERMOLE_STATUS_SYSTEM_ERROR;

    termole_cview* view = calloc(1, sizeof(*view));
    if (!view) return TERMOLE_STATUS_OUT_OF_MEMORY;
    view->session = session;
    const wchar_t* font = options && options->font_family ? options->font_family : L"Consolas";
    const int size = options && options->font_size > 0 ? options->font_size : 20;
    const int theme = options ? options->theme : 0;
    view->renderer = termole_gdi_create(font, size, theme);
    if (!view->renderer) {
        free(view);
        return TERMOLE_STATUS_OUT_OF_MEMORY;
    }

    view->window = CreateWindowExW(0, TERMOLE_CVIEW_CLASS, L"", WS_CHILD | WS_VISIBLE,
                                   0, 0, 0, 0, parent, NULL, GetModuleHandleW(NULL), view);
    if (!view->window) {
        termole_gdi_destroy(view->renderer);
        free(view);
        return TERMOLE_STATUS_SYSTEM_ERROR;
    }
    RECT parent_client = { 0, 0, 0, 0 };
    if (GetClientRect(parent, &parent_client)) {
        MoveWindow(view->window, 0, 0, parent_client.right, parent_client.bottom, TRUE);
    }
    SetTimer(view->window, TERMOLE_CVIEW_TIMER, 33, NULL);
    *out_view = view;
    return TERMOLE_STATUS_OK;
}

void termole_cview_destroy(termole_cview* view)
{
    if (!view) return;
    if (view->window) {
        KillTimer(view->window, TERMOLE_CVIEW_TIMER);
        DestroyWindow(view->window);
    }
    termole_gdi_destroy(view->renderer);
    termole_free(view->viewport.frame.cells);
    free(view);
}

HWND termole_cview_hwnd(const termole_cview* view)
{
    return view ? view->window : NULL;
}

void termole_cview_set_selection(termole_cview* view, int32_t start_row, int32_t start_column,
                                 int32_t end_row, int32_t end_column)
{
    if (!view) return;
    view->selection_anchor_row = start_row;
    view->selection_anchor_column = start_column;
    view->selection_focus_row = end_row;
    view->selection_focus_column = end_column;
    view->selection_active = 1;
    view->selection_drag = 0;
    InvalidateRect(view->window, NULL, FALSE);
}

void termole_cview_clear_selection(termole_cview* view)
{
    if (!view) return;
    view->selection_active = 0;
    view->selection_drag = 0;
    InvalidateRect(view->window, NULL, FALSE);
}

termole_status termole_cview_get_selection_text(termole_cview* view, char** out_text,
                                                size_t* out_size)
{
    if (!view || !out_text || !out_size) return TERMOLE_STATUS_INVALID_ARGUMENT;
    if (!view->selection_active) return TERMOLE_STATUS_INVALID_STATE;
    return termole_session_get_selection_text(view->session, view->selection_anchor_row,
                                              view->selection_anchor_column,
                                              view->selection_focus_row,
                                              view->selection_focus_column, out_text, out_size);
}
uint64_t termole_cview_paint_count(const termole_cview* view)
{
    return view ? view->paint_count : 0;
}
