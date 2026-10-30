#include "view/view.h"

#include "core/utf8.h"

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>

namespace termole {
namespace {

constexpr wchar_t kViewClassName[] = L"TermoleViewWindow";
constexpr UINT kRedrawMessage = WM_APP + 1;

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
                                          uint32_t font_size_dip)
{
    if (!session) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null"));
    if (!parent) return std::unexpected(make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "parent HWND is null"));
    if (font_size_dip != 0 && (font_size_dip < kMinFontSizeDip || font_size_dip > kMaxFontSizeDip)) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_ARGUMENT, "font size must be between 8 and 96 DIPs"));
    }

    auto view = std::unique_ptr<View>(new View(std::move(session), parent));
    if (!view->attached_) {
        return std::unexpected(
            make_error(TERMOLE_STATUS_INVALID_STATE, "session already has an attached view"));
    }
    auto initialized = view->initialize(font_size_dip);
    if (!initialized) return std::unexpected(initialized.error());
    return view;
}

Result<void> View::initialize(uint32_t font_size_dip)
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

    auto renderer = Renderer::create(window, font_size_dip);
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
        return 0;
    case WM_CHAR:
        on_character(static_cast<wchar_t>(wparam));
        return 0;
    case WM_KEYDOWN:
        on_key_down(wparam);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
    case kRedrawMessage:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        // Scrollback and mouse reporting belong to later slices.
        return 0;
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
        painted = renderer_->paint(session_->snapshot(), focused_);
    }
    if (!painted) {
        RECT client{};
        ::GetClientRect(window, &client);
        ::FillRect(device, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
    }
    ::EndPaint(window, &paint);
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
        (void)session_->send_key(TERMOLE_KEY_CTRL_C);
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
