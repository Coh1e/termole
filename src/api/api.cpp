#include <termole/termole.h>

#include "common/win.h"
#include "session/session.h"
#include "view/view.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>

namespace {

thread_local std::string last_error;

void clear_error()
{
    last_error.clear();
}

termole_status fail(termole_status status, std::string message)
{
    last_error = std::move(message);
    return status;
}

template <class Function>
termole_status guard(Function&& function)
{
    clear_error();
    try {
        return function();
    } catch (const std::bad_alloc&) {
        return fail(TERMOLE_STATUS_OUT_OF_MEMORY, "out of memory");
    } catch (const std::exception& exception) {
        return fail(TERMOLE_STATUS_SYSTEM_ERROR, exception.what());
    } catch (...) {
        return fail(TERMOLE_STATUS_SYSTEM_ERROR, "unknown exception");
    }
}

termole_status copy_string(const std::string& text, char** out_text, size_t* out_size)
{
    char* buffer = static_cast<char*>(std::malloc(text.size() + 1));
    if (!buffer) return fail(TERMOLE_STATUS_OUT_OF_MEMORY, "out of memory");
    if (!text.empty()) std::memcpy(buffer, text.data(), text.size());
    buffer[text.size()] = '\0';
    *out_text = buffer;
    if (out_size) *out_size = text.size();
    return TERMOLE_STATUS_OK;
}

} // namespace

struct termole_session {
    std::shared_ptr<termole::Session> implementation;
};

struct termole_view {
    std::shared_ptr<termole::Session> session;
    std::unique_ptr<termole::View> implementation;
};

extern "C" {

uint32_t TERMOLE_CALL termole_abi_version(void)
{
    return TERMOLE_ABI_VERSION;
}

const char* TERMOLE_CALL termole_last_error(void)
{
    return last_error.c_str();
}

void TERMOLE_CALL termole_free(void* memory)
{
    std::free(memory);
}

termole_status TERMOLE_CALL termole_session_create(const termole_session_options* options,
                                                   termole_session** out_session)
{
    return guard([&]() -> termole_status {
        if (!out_session) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "out_session is null");
        *out_session = nullptr;
        if (!options) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "options is null");
        if (options->size < sizeof(termole_session_options))
            return fail(TERMOLE_STATUS_ABI_MISMATCH, "termole_session_options is too small");
        if (options->abi_version != TERMOLE_ABI_VERSION)
            return fail(TERMOLE_STATUS_ABI_MISMATCH, "ABI version mismatch");

        termole::SessionOptions internal;
        if (options->program) internal.program = options->program;
        if (options->arguments) internal.arguments = options->arguments;
        if (options->working_directory) internal.working_directory = options->working_directory;
        internal.columns = options->columns;
        internal.rows = options->rows;

        auto created = termole::Session::create(internal);
        if (!created) return fail(created.error().status, created.error().message);

        try {
            auto* wrapper = new termole_session{ std::move(*created) };
            *out_session = wrapper;
            return TERMOLE_STATUS_OK;
        } catch (const std::bad_alloc&) {
            return fail(TERMOLE_STATUS_OUT_OF_MEMORY, "out of memory");
        }
    });
}

void TERMOLE_CALL termole_session_destroy(termole_session* session)
{
    try {
        delete session;
    } catch (...) {
        // No C++ exception may cross the C ABI, including from a destructor.
    }
}

termole_status TERMOLE_CALL termole_session_terminate(termole_session* session)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        const auto result = session->implementation->terminate();
        if (!result) return fail(result.error().status, result.error().message);
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_resize(termole_session* session, uint16_t columns,
                                                   uint16_t rows)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        const auto result = session->implementation->resize(columns, rows);
        if (!result) return fail(result.error().status, result.error().message);
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_write(termole_session* session, const void* data,
                                                  size_t size)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (size != 0 && !data) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "data is null");
        std::string_view bytes;
        if (size != 0) bytes = std::string_view(static_cast<const char*>(data), size);
        const auto result = session->implementation->write(bytes);
        if (!result) return fail(result.error().status, result.error().message);
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_send_key(termole_session* session, termole_key key)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        const auto result = session->implementation->send_key(key);
        if (!result) return fail(result.error().status, result.error().message);
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_get_size(termole_session* session, uint16_t* out_columns,
                                                     uint16_t* out_rows)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (!out_columns || !out_rows)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "size outputs are null");
        const auto [columns, rows] = session->implementation->size();
        *out_columns = columns;
        *out_rows = rows;
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_is_running(termole_session* session, int* out_running)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (!out_running) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "out_running is null");
        *out_running = session->implementation->running() ? 1 : 0;
        return TERMOLE_STATUS_OK;
    });
}

termole_status TERMOLE_CALL termole_session_get_screen_text(termole_session* session, char** out_text,
                                                            size_t* out_size)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (!out_text) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "out_text is null");
        *out_text = nullptr;
        if (out_size) *out_size = 0;
        return copy_string(session->implementation->screen_text(), out_text, out_size);
    });
}

termole_status TERMOLE_CALL termole_session_get_title(termole_session* session, char** out_text,
                                                      size_t* out_size)
{
    return guard([&]() -> termole_status {
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (!out_text) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "out_text is null");
        *out_text = nullptr;
        if (out_size) *out_size = 0;
        return copy_string(termole::win::wide_to_utf8(session->implementation->title()), out_text,
                           out_size);
    });
}

termole_status TERMOLE_CALL termole_view_create(termole_session* session, HWND parent,
                                                const termole_view_options* options,
                                                termole_view** out_view)
{
    return guard([&]() -> termole_status {
        if (!out_view) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "out_view is null");
        *out_view = nullptr;
        if (!session || !session->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "session is null");
        if (!parent || !::IsWindow(parent))
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "parent is not a window");
        if (!options) return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "options is null");
        if (options->size < sizeof(termole_view_options))
            return fail(TERMOLE_STATUS_ABI_MISMATCH, "termole_view_options is too small");
        if (options->abi_version != TERMOLE_ABI_VERSION)
            return fail(TERMOLE_STATUS_ABI_MISMATCH, "ABI version mismatch");

        auto created = termole::View::create(session->implementation, parent, options->font_size_dip);
        if (!created) return fail(created.error().status, created.error().message);

        try {
            auto* wrapper = new termole_view{ session->implementation, std::move(*created) };
            *out_view = wrapper;
            return TERMOLE_STATUS_OK;
        } catch (const std::bad_alloc&) {
            return fail(TERMOLE_STATUS_OUT_OF_MEMORY, "out of memory");
        }
    });
}

void TERMOLE_CALL termole_view_destroy(termole_view* view)
{
    try {
        delete view;
    } catch (...) {
        // No C++ exception may cross the C ABI, including from a destructor.
    }
}

HWND TERMOLE_CALL termole_view_hwnd(termole_view* view)
{
    return view && view->implementation ? view->implementation->hwnd() : nullptr;
}

termole_status TERMOLE_CALL termole_view_focus(termole_view* view)
{
    return guard([&]() -> termole_status {
        if (!view || !view->implementation)
            return fail(TERMOLE_STATUS_INVALID_ARGUMENT, "view is null");
        view->implementation->focus();
        return TERMOLE_STATUS_OK;
    });
}

} // extern "C"
