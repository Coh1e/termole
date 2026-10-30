/*
 * Termole C++23 wrapper.
 *
 * The wrapper adds RAII, strong types and std::expected results. It does not
 * reimplement behavior: every operation calls the same C ABI exported by
 * termole.dll, so native hosts and C hosts share one contract.
 */

#ifndef TERMOLE_TERMOLE_HPP
#define TERMOLE_TERMOLE_HPP

#include <termole/termole.h>

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace termole {

class Error {
public:
    Error(termole_status status, std::string message)
        : status_(status), message_(std::move(message))
    {
    }

    [[nodiscard]] termole_status status() const noexcept { return status_; }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }

private:
    termole_status status_;
    std::string message_;
};

enum class Key : int {
    Enter = TERMOLE_KEY_ENTER,
    Tab = TERMOLE_KEY_TAB,
    Backspace = TERMOLE_KEY_BACKSPACE,
    Escape = TERMOLE_KEY_ESCAPE,
    ArrowUp = TERMOLE_KEY_ARROW_UP,
    ArrowDown = TERMOLE_KEY_ARROW_DOWN,
    ArrowLeft = TERMOLE_KEY_ARROW_LEFT,
    ArrowRight = TERMOLE_KEY_ARROW_RIGHT,
    Home = TERMOLE_KEY_HOME,
    End = TERMOLE_KEY_END,
    Delete = TERMOLE_KEY_DELETE,
    Insert = TERMOLE_KEY_INSERT,
    PageUp = TERMOLE_KEY_PAGE_UP,
    PageDown = TERMOLE_KEY_PAGE_DOWN,
    CtrlC = TERMOLE_KEY_CTRL_C,
    CtrlD = TERMOLE_KEY_CTRL_D
};

namespace detail {

inline Error take_error(termole_status status)
{
    const char* message = termole_last_error();
    return Error{ status, message ? std::string{ message } : std::string{} };
}

inline std::expected<void, Error> check(termole_status status)
{
    if (status == TERMOLE_STATUS_OK) return {};
    return std::unexpected(take_error(status));
}

struct free_deleter {
    void operator()(void* memory) const noexcept { termole_free(memory); }
};

using unique_string = std::unique_ptr<char, free_deleter>;

} // namespace detail

class Session {
public:
    struct Options {
        std::optional<std::wstring> program;
        std::vector<std::wstring> arguments;
        std::optional<std::wstring> working_directory;
        std::vector<std::wstring> environment;
        uint16_t columns = 0;
        uint16_t rows = 0;
    };

    static std::expected<Session, Error> create(const Options& options = {})
    {
        termole_session_options raw = TERMOLE_SESSION_OPTIONS_INIT;
        raw.program = options.program && !options.program->empty() ? options.program->c_str() : nullptr;
        raw.working_directory = options.working_directory && !options.working_directory->empty()
            ? options.working_directory->c_str()
            : nullptr;
        raw.columns = options.columns;
        raw.rows = options.rows;

        std::vector<const wchar_t*> argument_pointers;
        argument_pointers.reserve(options.arguments.size());
        for (const auto& argument : options.arguments) argument_pointers.push_back(argument.c_str());
        raw.arguments = argument_pointers.empty() ? nullptr : argument_pointers.data();
        raw.argument_count = static_cast<uint32_t>(argument_pointers.size());

        std::vector<const wchar_t*> environment_pointers;
        environment_pointers.reserve(options.environment.size());
        for (const auto& entry : options.environment) environment_pointers.push_back(entry.c_str());
        raw.environment = environment_pointers.empty() ? nullptr : environment_pointers.data();
        raw.environment_count = static_cast<uint32_t>(environment_pointers.size());

        termole_session* handle = nullptr;
        const termole_status status = termole_session_create(&raw, &handle);
        if (status != TERMOLE_STATUS_OK) return std::unexpected(detail::take_error(status));
        return Session{ handle };
    }

    Session() noexcept = default;
    Session(Session&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Session& operator=(Session&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ~Session() { reset(); }

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] termole_session* native_handle() const noexcept { return handle_; }

    std::expected<void, Error> resize(uint16_t columns, uint16_t rows)
    {
        return detail::check(termole_session_resize(handle_, columns, rows));
    }

    std::expected<void, Error> write(std::string_view bytes)
    {
        return detail::check(termole_session_write(handle_, bytes.data(), bytes.size()));
    }

    std::expected<void, Error> send_key(Key key)
    {
        return detail::check(termole_session_send_key(handle_, static_cast<termole_key>(key)));
    }

    std::expected<std::pair<uint16_t, uint16_t>, Error> size() const
    {
        uint16_t columns = 0;
        uint16_t rows = 0;
        const termole_status status = termole_session_get_size(handle_, &columns, &rows);
        if (status != TERMOLE_STATUS_OK) return std::unexpected(detail::take_error(status));
        return std::pair{ columns, rows };
    }

    [[nodiscard]] bool running() const noexcept
    {
        int running = 0;
        return termole_session_is_running(handle_, &running) == TERMOLE_STATUS_OK && running != 0;
    }

    std::expected<std::string, Error> screen_text() const
    {
        char* text = nullptr;
        size_t size = 0;
        const termole_status status = termole_session_get_screen_text(handle_, &text, &size);
        if (status != TERMOLE_STATUS_OK) return std::unexpected(detail::take_error(status));
        detail::unique_string owned{ text };
        return std::string{ owned.get(), size };
    }

    std::expected<std::string, Error> title() const
    {
        char* text = nullptr;
        size_t size = 0;
        const termole_status status = termole_session_get_title(handle_, &text, &size);
        if (status != TERMOLE_STATUS_OK) return std::unexpected(detail::take_error(status));
        detail::unique_string owned{ text };
        return std::string{ owned.get(), size };
    }

    std::expected<void, Error> terminate()
    {
        if (!handle_) return std::unexpected(Error{ TERMOLE_STATUS_INVALID_STATE, "session is null" });
        return detail::check(termole_session_terminate(handle_));
    }

    void reset() noexcept
    {
        if (handle_) {
            termole_session_destroy(handle_);
            handle_ = nullptr;
        }
    }

private:
    explicit Session(termole_session* handle) noexcept : handle_(handle) {}

    termole_session* handle_ = nullptr;
};

class View {
public:
    struct Options {
        uint32_t font_size_dip = 0;
    };

    static std::expected<View, Error> create(Session& session, HWND parent, const Options& options = {})
    {
        if (!parent) return std::unexpected(Error{ TERMOLE_STATUS_INVALID_ARGUMENT, "parent HWND is null" });
        termole_view_options raw = TERMOLE_VIEW_OPTIONS_INIT;
        raw.font_size_dip = options.font_size_dip;
        termole_view* handle = nullptr;
        const termole_status status =
            termole_view_create(session.native_handle(), parent, &raw, &handle);
        if (status != TERMOLE_STATUS_OK) return std::unexpected(detail::take_error(status));
        return View{ handle };
    }

    View() noexcept = default;
    View(View&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    View& operator=(View&& other) noexcept
    {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    View(const View&) = delete;
    View& operator=(const View&) = delete;

    ~View() { reset(); }

    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] HWND hwnd() const noexcept { return handle_ ? termole_view_hwnd(handle_) : nullptr; }

    void focus() noexcept
    {
        if (handle_) termole_view_focus(handle_);
    }

    void reset() noexcept
    {
        if (handle_) {
            termole_view_destroy(handle_);
            handle_ = nullptr;
        }
    }

private:
    explicit View(termole_view* handle) noexcept : handle_(handle) {}

    termole_view* handle_ = nullptr;
};

} // namespace termole

#endif /* TERMOLE_TERMOLE_HPP */
