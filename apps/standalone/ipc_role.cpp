#include "standalone/ipc_role.h"

#include "common/diagnostics.h"
#include "common/exit_codes.h"
#include "core/input.h"
#include "ipc/session_ipc.h"
#include "view/renderer.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>

namespace termole::app {
namespace {
struct ClientWindow {
    std::unique_ptr<ipc::RemoteSession> session;
    std::unique_ptr<Renderer> renderer;
    Snapshot snapshot;
    std::optional<SessionFrame> frame;
    FrameSelection selection;
    uint64_t revision = 0;
    uint16_t columns = 0;
    uint16_t rows = 0;
    bool focused = false;
    wchar_t high_surrogate = 0;

    void resize(HWND window)
    {
        if (!renderer) return;
        RECT client{};
        ::GetClientRect(window, &client);
        const auto next_columns = static_cast<uint16_t>(std::clamp(
            static_cast<int>(client.right / renderer->cell_width()), 1, 1000));
        const auto next_rows = static_cast<uint16_t>(std::clamp(
            static_cast<int>(client.bottom / renderer->cell_height()), 1, 1000));
        if (next_columns == columns && next_rows == rows) return;
        std::string error;
        if (session->resize(next_columns, next_rows, error)) {
            columns = next_columns;
            rows = next_rows;
        }
    }

    void write(std::string_view bytes)
    {
        std::string error;
        (void)session->write(bytes, error);
    }

    void character(wchar_t value)
    {
        if (value >= 0xd800 && value <= 0xdbff) {
            high_surrogate = value;
            return;
        }
        wchar_t text[]{ value, 0 };
        size_t length = 1;
        if (high_surrogate && value >= 0xdc00 && value <= 0xdfff) {
            text[0] = high_surrogate;
            text[1] = value;
            length = 2;
        }
        high_surrogate = 0;
        if (value == L'\b') text[0] = L'\x7f';
        write(win::wide_to_utf8({ text, length }));
    }

    bool key(WPARAM value)
    {
        termole_key logical;
        switch (value) {
        case VK_UP: logical = TERMOLE_KEY_ARROW_UP; break;
        case VK_DOWN: logical = TERMOLE_KEY_ARROW_DOWN; break;
        case VK_RIGHT: logical = TERMOLE_KEY_ARROW_RIGHT; break;
        case VK_LEFT: logical = TERMOLE_KEY_ARROW_LEFT; break;
        case VK_HOME: logical = TERMOLE_KEY_HOME; break;
        case VK_END: logical = TERMOLE_KEY_END; break;
        case VK_INSERT: logical = TERMOLE_KEY_INSERT; break;
        case VK_DELETE: logical = TERMOLE_KEY_DELETE; break;
        case VK_PRIOR: logical = TERMOLE_KEY_PAGE_UP; break;
        case VK_NEXT: logical = TERMOLE_KEY_PAGE_DOWN; break;
        default: return false;
        }
        write(termole_key_sequence(logical, session->modes().application_cursor));
        return true;
    }

    void select(HWND window, LPARAM mouse, bool start, bool end)
    {
        if (!renderer || !frame || (!start && !selection.dragging)) return;
        const int column = std::clamp(
            static_cast<int>(GET_X_LPARAM(mouse) / renderer->cell_width()), 0, frame->columns - 1);
        const int row = std::clamp(
            static_cast<int>(GET_Y_LPARAM(mouse) / renderer->cell_height()), 0, frame->rows - 1);
        const size_t index = static_cast<size_t>(row) * frame->columns + column;
        if (start) {
            selection.begin(index);
            ::SetCapture(window);
        } else {
            selection.focus = index;
        }
        if (end) {
            selection.dragging = false;
            ::ReleaseCapture();
        }
        update_selection();
        ::InvalidateRect(window, nullptr, FALSE);
    }

    void update_selection()
    {
        const auto selected = selection.range(snapshot.cells.size());
        snapshot.has_selection = selected.has_value();
        if (!selected) return;
        snapshot.selection_start_row = static_cast<int16_t>(selected->first / snapshot.columns);
        snapshot.selection_start_column = static_cast<int16_t>(selected->first % snapshot.columns);
        snapshot.selection_end_row = static_cast<int16_t>(selected->second / snapshot.columns);
        snapshot.selection_end_column = static_cast<int16_t>(selected->second % snapshot.columns);
    }

    void clipboard(HWND window, bool copy)
    {
        if (!::OpenClipboard(window)) return;
        std::string pasted;
        if (copy && frame && selection.anchor) {
            const auto text = utf8_to_wide(selection.text(*frame));
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            HGLOBAL storage = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (storage) {
                if (void* data = ::GlobalLock(storage)) {
                    std::memcpy(data, text.c_str(), bytes);
                    ::GlobalUnlock(storage);
                    if (::EmptyClipboard() && ::SetClipboardData(CF_UNICODETEXT, storage))
                        storage = nullptr;
                }
                if (storage) ::GlobalFree(storage);
            }
        } else if (!copy) {
            if (HGLOBAL storage = ::GetClipboardData(CF_UNICODETEXT)) {
                if (const auto* data = static_cast<const wchar_t*>(::GlobalLock(storage))) {
                    const size_t capacity = ::GlobalSize(storage) / sizeof(wchar_t);
                    if (const auto* end = std::wmemchr(data, L'\0', capacity))
                        pasted = win::wide_to_utf8({ data, static_cast<size_t>(end - data) });
                    ::GlobalUnlock(storage);
                }
            }
        }
        ::CloseClipboard();
        if (!pasted.empty()) {
            if (session->modes().bracketed_paste) bracket_paste(pasted);
            write(pasted);
        }
    }

    void fetch_frame(HWND window)
    {
        auto next = session->frame_after(revision);
        if (!next) return;
        revision = next->revision;
        snapshot.columns = next->columns;
        snapshot.rows = next->rows;
        snapshot.cursor_column = next->cursor_column;
        snapshot.cursor_row = next->cursor_row;
        snapshot.cursor_visible = next->cursor_visible;
        snapshot.cells.resize(next->cells.size());
        for (size_t index = 0; index < next->cells.size(); ++index) {
            const auto& cell = next->cells[index];
            auto& result = snapshot.cells[index];
            result.character = cell.codepoint_count ? static_cast<char32_t>(cell.codepoints[0]) : U' ';
            result.combining.clear();
            for (uint8_t codepoint = 1; codepoint < cell.codepoint_count; ++codepoint)
                result.combining.push_back(static_cast<char32_t>(cell.codepoints[codepoint]));
            result.width = cell.width;
            result.continuation = cell.continuation != 0;
            result.foreground = cell.foreground_is_default ? kColorDefault : cell.foreground;
            result.background = cell.background_is_default ? kColorDefault : cell.background;
            result.attributes = cell.attributes;
        }
        frame = std::move(next);
        update_selection();
        ::InvalidateRect(window, nullptr, FALSE);
    }

    void draw(HWND window, HDC device)
    {
        RECT client{};
        ::GetClientRect(window, &client);
        if (!renderer || !renderer->paint(device, client, snapshot, focused))
            ::FillRect(device, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    }
};

LRESULT CALLBACK client_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* client = reinterpret_cast<ClientWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!client) return ::DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC device = ::BeginPaint(window, &paint);
        client->draw(window, device);
        ::EndPaint(window, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        client->draw(window, reinterpret_cast<HDC>(wparam));
        return 0;
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED) client->resize(window);
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        client->focused = message == WM_SETFOCUS;
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        ::SetFocus(window);
        client->select(window, lparam, true, false);
        return 0;
    case WM_MOUSEMOVE:
        client->select(window, lparam, false, false);
        return 0;
    case WM_LBUTTONUP:
        client->select(window, lparam, false, true);
        return 0;
    case WM_CAPTURECHANGED:
        client->selection.dragging = false;
        return 0;
    case WM_KEYDOWN:
        if ((wparam == 'C' || wparam == 'V') &&
            ::GetKeyState(VK_CONTROL) < 0 && ::GetKeyState(VK_SHIFT) < 0) {
            client->clipboard(window, wparam == 'C');
            return 0;
        }
        if (client->key(wparam)) return 0;
        break;
    case WM_CHAR:
        if ((wparam == 3 || wparam == 22) &&
            ::GetKeyState(VK_CONTROL) < 0 && ::GetKeyState(VK_SHIFT) < 0) return 0;
        client->character(static_cast<wchar_t>(wparam));
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

int show_client(std::unique_ptr<ipc::RemoteSession> session)
{
    ClientWindow state{};
    state.session = std::move(session);
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = client_window_proc;
    type.hInstance = instance;
    type.hCursor = ::LoadCursorW(nullptr, IDC_IBEAM);
    type.lpszClassName = L"TermoleRemoteClient";
    if (!::RegisterClassW(&type)) return kExitInfrastructure;
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HWND window = ::CreateWindowExW(0, type.lpszClassName, L"Termole Client",
                                          WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                          1000, 640, nullptr, nullptr, instance, &state);
    if (!window) return kExitInfrastructure;
    auto renderer = Renderer::create(window, {});
    if (!renderer) {
        ::DestroyWindow(window);
        return kExitInfrastructure;
    }
    state.renderer = std::move(*renderer);
    state.resize(window);
    ::ShowWindow(window, SW_SHOW);
    int result = 0;
    for (;;) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return result;
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        state.fetch_frame(window);
        if (!state.session->running()) {
            result = state.session->wait();
            ::DestroyWindow(window);
            continue;
        }
        (void)::MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
} // namespace

int run_ipc_role(int argc, wchar_t** argv)
{
    const bool server = std::wstring_view(argv[1]) == L"--server";
    std::string name = "default";
    bool detach = false;
    ipc::ServerSessionOptions options;
    for (int index = 2; index < argc; ++index) {
        const std::wstring_view option = argv[index];
        if (option == L"--pipe" && index + 1 < argc) {
            name = win::wide_to_utf8(argv[++index]);
        } else if (option == L"--detach" && !server) {
            detach = true;
        } else if (option == L"--" && server && index + 1 < argc) {
            options.program = argv[++index];
            for (++index; index < argc; ++index) options.arguments.emplace_back(argv[index]);
            break;
        } else {
            (void)write_standard_error(L"Invalid Termole server or client arguments.\n");
            return kExitUsage;
        }
    }
    if (server) return ipc::run_server(name, options);
    std::string error;
    if (auto session = ipc::RemoteSession::connect(name, detach, error))
        return show_client(std::move(session));
    (void)write_standard_error(utf8_to_wide(error) + L"\n");
    return kExitInfrastructure;
}
} // namespace termole::app
