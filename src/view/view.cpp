#include "view/view.h"

#include "core/utf8.h"

#include <windowsx.h>
#include <imm.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace termole {
namespace {

constexpr wchar_t kViewClassName[] = L"TermoleViewWindow";
constexpr UINT kRedrawMessage = WM_APP + 1;

std::wstring wide_from_utf8(std::string_view text)
{
    if (text.empty()) return {};
    const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                             static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

bool write_clipboard(std::wstring_view text)
{
    bool opened = false;
    for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
        opened = ::OpenClipboard(nullptr) != FALSE;
        if (!opened) ::Sleep(2);
    }
    if (!opened) return false;
    ::EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        ::CloseClipboard();
        return false;
    }
    void* data = ::GlobalLock(memory);
    if (!data) {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }
    std::memcpy(data, text.data(), text.size() * sizeof(wchar_t));
    static_cast<wchar_t*>(data)[text.size()] = L'\0';
    ::GlobalUnlock(memory);
    if (!::SetClipboardData(CF_UNICODETEXT, memory)) {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }
    ::CloseClipboard();
    return true;
}

std::wstring read_clipboard()
{
    bool opened = false;
    for (int attempt = 0; attempt < 100 && !opened; ++attempt) {
        opened = ::OpenClipboard(nullptr) != FALSE;
        if (!opened) ::Sleep(2);
    }
    if (!opened) return {};
    std::wstring text;
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data) {
        const auto* characters = static_cast<const wchar_t*>(::GlobalLock(data));
        if (characters) {
            text.assign(characters);
            ::GlobalUnlock(data);
        }
    }
    ::CloseClipboard();
    return text;
}

int mouse_modifiers()
{
    int modifiers = 0;
    if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) modifiers |= 4;
    if ((::GetKeyState(VK_MENU) & 0x8000) != 0) modifiers |= 8;
    if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) modifiers |= 16;
    return modifiers;
}

std::string mouse_sequence(int button, int column, int row, bool press, bool sgr)
{
    column = std::max(1, column);
    row = std::max(1, row);
    if (sgr) {
        return "\x1b[<" + std::to_string(button) + ";" + std::to_string(column) + ";" +
            std::to_string(row) + (press ? "M" : "m");
    }
    const int encoded_button = press ? button : 3;
    std::string sequence = "\x1b[M";
    sequence.push_back(static_cast<char>(32 + encoded_button));
    sequence.push_back(static_cast<char>(32 + column));
    sequence.push_back(static_cast<char>(32 + row));
    return sequence;
}

HMODULE module_instance()
{
    HMODULE module = nullptr;
    if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&View::window_proc), &module)) {
        return module;
    }
    return ::GetModuleHandleW(nullptr);
}

bool register_view_class()
{
    static std::once_flag once;
    static bool registered = false;
    std::call_once(once, [] {
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        window_class.lpfnWndProc = &View::window_proc;
        window_class.hInstance = module_instance();
        window_class.hCursor = ::LoadCursorW(nullptr, IDC_IBEAM);
        window_class.lpszClassName = kViewClassName;
        registered = ::RegisterClassExW(&window_class) != 0;
        if (!registered && ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS) registered = true;
    });
    return registered;
}

} // namespace

View::View(std::shared_ptr<Session> session, HWND parent)
    : session_(std::move(session)), parent_(parent), attached_(session_ && session_->attach_view())
{
}

View::~View()
{
    stop_watcher();
    HWND window = window_.exchange(nullptr, std::memory_order_acq_rel);
    if (window) ::DestroyWindow(window);
    renderer_.reset();
    if (attached_) session_->detach_view();
}

Result<std::unique_ptr<View>> View::create(std::shared_ptr<Session> session, HWND parent,
                                          const Options& options)
{
    if (!session) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null"));
    if (!parent) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "parent HWND is null"));
    if (options.font_size_dip != 0 &&
        (options.font_size_dip < kMinFontSizeDip || options.font_size_dip > kMaxFontSizeDip)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "font size must be between 8 and 96 DIPs"));
    }
    if (options.theme > static_cast<uint32_t>(Theme::Light)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "unknown theme"));
    }

    auto view = std::unique_ptr<View>(new View(std::move(session), parent));
    if (!view->attached_) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session already has an attached view"));
    }
    auto initialized = view->initialize(options);
    if (!initialized) return std::unexpected(initialized.error());
    return view;
}

Result<void> View::set_appearance(const Options& options)
{
    if (!renderer_) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "view is not initialized"));
    }
    if (options.font_size_dip != 0 &&
        (options.font_size_dip < kMinFontSizeDip || options.font_size_dip > kMaxFontSizeDip)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "font size must be between 8 and 96 DIPs"));
    }
    if (options.theme > static_cast<uint32_t>(Theme::Light)) {
        return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "unknown theme"));
    }
    Appearance appearance{
        options.font_family.empty() ? L"Consolas" : options.font_family,
        options.font_size_dip == 0 ? 16u : options.font_size_dip,
        static_cast<Theme>(options.theme),
    };
    auto applied = renderer_->set_appearance(appearance);
    if (!applied) return applied;
    appearance_ = std::move(appearance);
    on_size();
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
    return {};
}

Result<void> View::initialize(const Options& options)
{
    if (!register_view_class()) {
        return std::unexpected(win::system_error("RegisterClassExW", ::GetLastError()));
    }

    const HWND window = ::CreateWindowExW(
        0, kViewClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent_, nullptr,
        module_instance(), this);
    if (!window) {
        return std::unexpected(win::system_error("CreateWindowExW", ::GetLastError()));
    }

    appearance_ = Appearance{
        options.font_family.empty() ? L"Consolas" : options.font_family,
        options.font_size_dip == 0 ? 16u : options.font_size_dip,
        static_cast<Theme>(options.theme),
    };
    auto renderer = Renderer::create(window, appearance_);
    if (!renderer) {
        auto error = renderer.error();
        ::DestroyWindow(window);
        window_.store(nullptr, std::memory_order_release);
        return std::unexpected(std::move(error));
    }
    renderer_ = std::move(*renderer);

    on_size();
    auto watcher = start_watcher();
    if (!watcher) {
        auto error = watcher.error();
        ::DestroyWindow(window);
        window_.store(nullptr, std::memory_order_release);
        return std::unexpected(std::move(error));
    }
    return {};
}

LRESULT CALLBACK View::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        auto* self = static_cast<View*>(create->lpCreateParams);
        if (self) {
            self->window_.store(window, std::memory_order_release);
            ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
    }

    auto* self = reinterpret_cast<View*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    LRESULT result = self ? self->handle_message(window, message, wparam, lparam)
                          : ::DefWindowProcW(window, message, wparam, lparam);

    if (message == WM_NCDESTROY) {
        if (self) self->window_.store(nullptr, std::memory_order_release);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
    }
    return result;
}

LRESULT View::handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_PAINT:
        on_paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        on_size();
        return 0;
    case WM_DPICHANGED:
        if (renderer_) renderer_->set_dpi(LOWORD(wparam));
        on_size();
        return 0;
    case WM_SETFOCUS:
        focused_ = true;
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_KILLFOCUS:
        focused_ = false;
        high_surrogate_ = 0;
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        ::SetFocus(window);
        on_left_button_down(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam),
                            (wparam & MK_SHIFT) != 0);
        return 0;
    case WM_LBUTTONUP:
        on_left_button_up();
        return 0;
    case WM_MOUSEMOVE:
        on_mouse_move(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        return 0;
    case WM_CHAR:
        on_character(static_cast<wchar_t>(wparam));
        return 0;
    case WM_KEYDOWN:
        on_key_down(wparam);
        return 0;
    case WM_IME_STARTCOMPOSITION:
        position_ime();
        return ::DefWindowProcW(window, message, wparam, lparam);
    case WM_IME_COMPOSITION:
        position_ime();
        on_ime_composition(lparam);
        return ::DefWindowProcW(window, message, wparam, lparam);
    case WM_IME_ENDCOMPOSITION:
        ::InvalidateRect(window, nullptr, FALSE);
        return ::DefWindowProcW(window, message, wparam, lparam);
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
    case kRedrawMessage:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ::ScreenToClient(window, &point);
        on_mouse_wheel(GET_WHEEL_DELTA_WPARAM(wparam), (wparam & MK_SHIFT) != 0);
        return 0;
    }
    case WM_SETCURSOR:
        ::SetCursor(::LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;
    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

void View::on_size()
{
    const HWND window = window_.load(std::memory_order_acquire);
    if (!window || !renderer_) return;

    RECT client{};
    if (!::GetClientRect(window, &client)) return;
    const int width = std::max<LONG>(0, client.right - client.left);
    const int height = std::max<LONG>(0, client.bottom - client.top);

    renderer_->resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    if (width == 0 || height == 0 || renderer_->cell_width() <= 0.0f ||
        renderer_->cell_height() <= 0.0f) {
        return;
    }

    const int columns = std::clamp(static_cast<int>(static_cast<float>(width) / renderer_->cell_width()),
                                   static_cast<int>(kMinColumns), static_cast<int>(kMaxColumns));
    const int rows = std::clamp(static_cast<int>(static_cast<float>(height) / renderer_->cell_height()),
                                static_cast<int>(kMinRows), static_cast<int>(kMaxRows));
    if (columns != last_columns_ || rows != last_rows_) {
        last_columns_ = static_cast<uint16_t>(columns);
        last_rows_ = static_cast<uint16_t>(rows);
        (void)session_->resize(last_columns_, last_rows_);
    }
    scroll_offset_ = std::min(scroll_offset_, static_cast<int>(session_->scrollback_line_count()));
    ::InvalidateRect(window, nullptr, FALSE);
}

void View::on_paint()
{
    const HWND window = window_.load(std::memory_order_acquire);
    if (!window) return;

    PAINTSTRUCT paint{};
    HDC device = ::BeginPaint(window, &paint);
    bool painted = false;
    if (renderer_) {
        Snapshot snapshot = session_->snapshot(scroll_offset_);
        if (has_selection_) {
            const int history = static_cast<int>(session_->scrollback_line_count());
            const int first = history - scroll_offset_;
            snapshot.has_selection = true;
            snapshot.selection_start_row = static_cast<int16_t>(selection_anchor_row_ - first);
            snapshot.selection_start_column = static_cast<int16_t>(selection_anchor_column_);
            snapshot.selection_end_row = static_cast<int16_t>(selection_focus_row_ - first);
            snapshot.selection_end_column = static_cast<int16_t>(selection_focus_column_);
        }
        painted = renderer_->paint(snapshot, focused_);
    }
    if (!painted) {
        RECT client{};
        ::GetClientRect(window, &client);
        ::FillRect(device, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    }
    ::EndPaint(window, &paint);
}

void View::scroll(int lines)
{
    if (!session_) return;
    const int history = static_cast<int>(session_->scrollback_line_count());
    const int next = std::clamp(scroll_offset_ + lines, 0, history);
    if (next == scroll_offset_) return;
    scroll_offset_ = next;
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
}

void View::set_selection(int start_row, int start_column, int end_row, int end_column)
{
    if (!session_) return;
    const int history = static_cast<int>(session_->scrollback_line_count());
    const int last = history + static_cast<int>(session_->size().second) - 1;
    selection_anchor_row_ = std::clamp(start_row, 0, last);
    selection_focus_row_ = std::clamp(end_row, 0, last);
    selection_anchor_column_ = std::clamp(start_column, 0, static_cast<int>(session_->size().first) - 1);
    selection_focus_column_ = std::clamp(end_column, 0, static_cast<int>(session_->size().first) - 1);
    has_selection_ = true;
    selecting_ = false;
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
}

void View::clear_selection()
{
    has_selection_ = false;
    selecting_ = false;
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
}

std::string View::selection_text() const
{
    if (!has_selection_ || !session_) return {};
    return session_->selection_text(selection_anchor_row_, selection_anchor_column_,
                                    selection_focus_row_, selection_focus_column_);
}

std::string View::viewport_text() const
{
    if (!session_) return {};
    const Snapshot snapshot = session_->snapshot(scroll_offset_);
    std::vector<std::string> lines;
    int last = -1;
    for (int row = 0; row < snapshot.rows; ++row) {
        std::string line;
        for (int column = 0; column < snapshot.columns; ++column) {
            const Cell& cell = snapshot.cells[static_cast<size_t>(row) * snapshot.columns + column];
            if (cell.continuation) continue;
            append_utf8(line, cell.character == 0 ? U' ' : cell.character);
            for (const char32_t combining : cell.combining) append_utf8(line, combining);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (!line.empty()) last = row;
        lines.push_back(std::move(line));
    }
    std::string text;
    for (int row = 0; row <= last; ++row) {
        text += lines[static_cast<size_t>(row)];
        if (row != last) text.push_back('\n');
    }
    return text;
}

Result<void> View::copy_selection()
{
    const std::string text = selection_text();
    if (text.empty()) return {};
    if (!write_clipboard(wide_from_utf8(text))) {
        return std::unexpected(win::system_error("SetClipboardData", ::GetLastError()));
    }
    return {};
}

Result<void> View::paste()
{
    const std::wstring text = read_clipboard();
    if (text.empty()) return {};
    std::string bytes = win::wide_to_utf8(text);
    if (session_->bracketed_paste()) {
        std::string wrapped = "\x1b[200~";
        wrapped += bytes;
        wrapped += "\x1b[201~";
        return send_text(wrapped);
    }
    return send_text(bytes);
}

bool View::selected_cell(int row, int column) const
{
    if (!has_selection_) return false;
    int first_row = selection_anchor_row_;
    int first_column = selection_anchor_column_;
    int last_row = selection_focus_row_;
    int last_column = selection_focus_column_;
    if (first_row > last_row || (first_row == last_row && first_column > last_column)) {
        std::swap(first_row, last_row);
        std::swap(first_column, last_column);
    }
    if (row < first_row || row > last_row) return false;
    if (row == first_row && column < first_column) return false;
    if (row == last_row && column > last_column) return false;
    return true;
}

bool View::mouse_coordinates(int x, int y, int& row, int& column) const
{
    if (!session_ || !renderer_ || renderer_->cell_width() <= 0.0f ||
        renderer_->cell_height() <= 0.0f) {
        return false;
    }
    const auto [columns, rows] = session_->size();
    int screen_row = static_cast<int>(static_cast<float>(y) / renderer_->cell_height());
    int screen_column = static_cast<int>(static_cast<float>(x) / renderer_->cell_width());
    screen_row = std::clamp(screen_row, 0, static_cast<int>(rows) - 1);
    screen_column = std::clamp(screen_column, 0, static_cast<int>(columns) - 1);
    const int history = static_cast<int>(session_->scrollback_line_count());
    row = std::clamp(history - scroll_offset_ + screen_row, 0, history + static_cast<int>(rows) - 1);
    column = screen_column;
    return true;
}

bool View::mouse_screen_coordinates(int x, int y, int& row, int& column) const
{
    if (!session_ || !renderer_ || renderer_->cell_width() <= 0.0f ||
        renderer_->cell_height() <= 0.0f) {
        return false;
    }
    const auto [columns, rows] = session_->size();
    row = std::clamp(static_cast<int>(static_cast<float>(y) / renderer_->cell_height()), 0,
                     static_cast<int>(rows) - 1);
    column = std::clamp(static_cast<int>(static_cast<float>(x) / renderer_->cell_width()), 0,
                        static_cast<int>(columns) - 1);
    return true;
}

void View::report_mouse(int button, int x, int y, bool press)
{
    int row = 0;
    int column = 0;
    if (!mouse_screen_coordinates(x, y, row, column)) return;
    const int encoded = button + mouse_modifiers();
    (void)session_->write(mouse_sequence(encoded, column + 1, row + 1, press, session_->sgr_mouse()));
}

void View::on_left_button_down(int x, int y, bool shift)
{
    ::SetFocus(window_.load(std::memory_order_acquire));
    if (session_->mouse_mode() != 0 && !shift) {
        mouse_reporting_ = true;
        mouse_button_ = 0;
        report_mouse(0, x, y, true);
        return;
    }
    int row = 0;
    int column = 0;
    if (!mouse_coordinates(x, y, row, column)) return;
    selection_anchor_row_ = row;
    selection_anchor_column_ = column;
    selection_focus_row_ = row;
    selection_focus_column_ = column;
    has_selection_ = true;
    selecting_ = true;
    ::SetCapture(window_.load(std::memory_order_acquire));
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
}

void View::on_left_button_up()
{
    if (mouse_reporting_) {
        POINT point{};
        ::GetCursorPos(&point);
        ::ScreenToClient(window_.load(std::memory_order_acquire), &point);
        report_mouse(mouse_button_, point.x, point.y, false);
        mouse_reporting_ = false;
        return;
    }
    if (!selecting_) return;
    selecting_ = false;
    ::ReleaseCapture();
}

void View::on_mouse_move(int x, int y)
{
    if (mouse_reporting_ && session_->mouse_mode() >= 2) {
        report_mouse(mouse_button_ + 32, x, y, true);
        return;
    }
    if (!selecting_) return;
    int row = 0;
    int column = 0;
    if (!mouse_coordinates(x, y, row, column)) return;
    selection_focus_row_ = row;
    selection_focus_column_ = column;
    if (const HWND window = window_.load(std::memory_order_acquire))
        ::InvalidateRect(window, nullptr, FALSE);
}

void View::on_mouse_wheel(int delta, bool shift)
{
    if (session_->mouse_mode() != 0 && !shift) {
        POINT point{};
        ::GetCursorPos(&point);
        ::ScreenToClient(window_.load(std::memory_order_acquire), &point);
        report_mouse(delta > 0 ? 64 : 65, point.x, point.y, true);
        return;
    }
    scroll(delta > 0 ? 3 : -3);
}

void View::position_ime()
{
    const HWND window = window_.load(std::memory_order_acquire);
    if (!window || !session_ || !renderer_) return;
    HIMC context = ::ImmGetContext(window);
    if (!context) return;
    const Snapshot snapshot = session_->snapshot(scroll_offset_);
    POINT point{
        static_cast<LONG>(snapshot.cursor_column * renderer_->cell_width()),
        static_cast<LONG>(snapshot.cursor_row * renderer_->cell_height()),
    };
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = point;
    ::ImmSetCompositionWindow(context, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = point;
    ::ImmSetCandidateWindow(context, &candidate);
    ::ImmReleaseContext(window, context);
}

void View::on_ime_composition(LPARAM flags)
{
    if ((flags & GCS_RESULTSTR) == 0) return;
    const HWND window = window_.load(std::memory_order_acquire);
    if (!window) return;
    HIMC context = ::ImmGetContext(window);
    if (!context) return;
    const LONG length = ::ImmGetCompositionStringW(context, GCS_RESULTSTR, nullptr, 0);
    if (length > 0) {
        std::wstring text(static_cast<size_t>(length) / sizeof(wchar_t), L'\0');
        ::ImmGetCompositionStringW(context, GCS_RESULTSTR, text.data(), length);
        (void)send_text(win::wide_to_utf8(text));
    }
    ::ImmReleaseContext(window, context);
    ::InvalidateRect(window, nullptr, FALSE);
}

void View::on_character(wchar_t character)
{
    char32_t codepoint = 0;
    if (character >= 0xD800 && character <= 0xDBFF) {
        high_surrogate_ = character;
        return;
    }
    if (character >= 0xDC00 && character <= 0xDFFF) {
        if (high_surrogate_ == 0) return;
        codepoint = 0x10000 + ((static_cast<char32_t>(high_surrogate_) - 0xD800) << 10) +
            (static_cast<char32_t>(character) - 0xDC00);
        high_surrogate_ = 0;
    } else {
        high_surrogate_ = 0;
        codepoint = static_cast<char32_t>(character);
    }

    switch (codepoint) {
    case U'\r':
    case U'\n':
        (void)session_->send_key(TERMOLE_KEY_ENTER);
        break;
    case U'\b':
    case 0x7F:
        (void)session_->send_key(TERMOLE_KEY_BACKSPACE);
        break;
    case U'\t':
        (void)session_->send_key(TERMOLE_KEY_TAB);
        break;
    case 0x1B:
        (void)session_->send_key(TERMOLE_KEY_ESCAPE);
        break;
    case 0x03:
        if (has_selection_) (void)copy_selection();
        else (void)session_->send_key(TERMOLE_KEY_CTRL_C);
        break;
    case 0x16:
        (void)paste();
        break;
    default:
        if (codepoint < 0x20) {
            const char byte = static_cast<char>(codepoint);
            (void)send_text(std::string_view{ &byte, 1 });
        } else {
            (void)send_text(utf8_encode(codepoint));
        }
        break;
    }
}

void View::on_key_down(WPARAM key)
{
    if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        if (key == 'C' && has_selection_) {
            (void)copy_selection();
            return;
        }
        if (key == 'V') {
            (void)paste();
            return;
        }
    }

    termole_key translated = TERMOLE_KEY_ENTER;
    switch (key) {
    case VK_UP:
        translated = TERMOLE_KEY_ARROW_UP;
        break;
    case VK_DOWN:
        translated = TERMOLE_KEY_ARROW_DOWN;
        break;
    case VK_RIGHT:
        translated = TERMOLE_KEY_ARROW_RIGHT;
        break;
    case VK_LEFT:
        translated = TERMOLE_KEY_ARROW_LEFT;
        break;
    case VK_HOME:
        translated = TERMOLE_KEY_HOME;
        break;
    case VK_END:
        translated = TERMOLE_KEY_END;
        break;
    case VK_DELETE:
        translated = TERMOLE_KEY_DELETE;
        break;
    case VK_INSERT:
        translated = TERMOLE_KEY_INSERT;
        break;
    case VK_PRIOR:
        translated = TERMOLE_KEY_PAGE_UP;
        break;
    case VK_NEXT:
        translated = TERMOLE_KEY_PAGE_DOWN;
        break;
    default:
        return;
    }
    (void)session_->send_key(translated);
}

Result<void> View::send_text(std::string_view bytes)
{
    if (!session_) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_STATE, "view has no session"));
    return session_->write(bytes);
}

Result<void> View::start_watcher()
{
    stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stop_event_) return std::unexpected(win::system_error("CreateEventW", ::GetLastError()));

    const HANDLE data_event = session_ ? session_->data_event() : nullptr;
    if (!data_event) {
        ::CloseHandle(stop_event_);
        stop_event_ = nullptr;
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session has no data event"));
    }

    try {
        watcher_ = std::thread([this, data_event] {
            const HANDLE handles[2]{ data_event, stop_event_ };
            for (;;) {
                const DWORD result = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (result == WAIT_OBJECT_0) {
                    const HWND window = window_.load(std::memory_order_acquire);
                    if (window) ::PostMessageW(window, kRedrawMessage, 0, 0);
                } else {
                    break;
                }
            }
        });
    } catch (const std::exception& exception) {
        ::CloseHandle(stop_event_);
        stop_event_ = nullptr;
        return std::unexpected(make_error(TERMOLE_STATUS_SYSTEM_ERROR, exception.what()));
    } catch (...) {
        ::CloseHandle(stop_event_);
        stop_event_ = nullptr;
        return std::unexpected(
            make_error(TERMOLE_STATUS_SYSTEM_ERROR, "failed to start the view watcher thread"));
    }
    return {};
}

void View::stop_watcher()
{
    if (stop_event_) {
        ::SetEvent(stop_event_);
    }
    if (watcher_.joinable()) watcher_.join();
    if (stop_event_) {
        ::CloseHandle(stop_event_);
        stop_event_ = nullptr;
    }
}

void View::focus()
{
    if (const HWND window = window_.load(std::memory_order_acquire)) ::SetFocus(window);
}

} // namespace termole
