/*
 * Termole public C ABI.
 *
 * Threading and lifetime contract
 * ------------------------------
 * - A Session may be created on any thread. Session operations are internally
 *   synchronized and may be called from any thread, except that
 *   termole_session_destroy must not race another call on the same handle.
 * - A View is a native child window and every View call must happen on the UI
 *   thread that owns the parent HWND. The Session must be kept alive by the
 *   caller while a View exists; the library also retains a reference so a View
 *   does not observe a destroyed Session.
 * - The library never calls a host callback from its I/O thread. This slice
 *   redraws through the View's own window messages.
 *
 * Data ownership
 * --------------
 * - Input buffers are only borrowed for the duration of a call and copied when
 *   they must be retained.
 * - Strings returned by the library are allocated by the library and must be
 *   released with termole_free(). They are NUL terminated and the returned
 *   size excludes the terminator.
 *
 * Errors
 * ------
 * Every fallible entry point returns termole_status and never lets a C++
 * exception cross the ABI. termole_last_error() returns a thread-local UTF-8
 * diagnostic valid until the next termole call on the same thread.
 *
 * ABI compatibility
 * -----------------
 * Options structures begin with size and abi_version. Callers initialize them
 * with the TERMOLE_*_OPTIONS_INIT macros. A size smaller than the version this
 * DLL was built with, or a different abi_version, is rejected explicitly.
 */

#ifndef TERMOLE_TERMOLE_H
#define TERMOLE_TERMOLE_H

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(TERMOLE_BUILDING_DLL)
#define TERMOLE_API __declspec(dllexport)
#elif defined(TERMOLE_STATIC)
#define TERMOLE_API
#else
#define TERMOLE_API __declspec(dllimport)
#endif
#define TERMOLE_CALL __cdecl
#else
#error "Termole only supports Windows."
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TERMOLE_ABI_VERSION 1u

typedef struct termole_session termole_session;
typedef struct termole_view termole_view;

typedef enum termole_status {
    TERMOLE_STATUS_OK = 0,
    /* A required argument was null or outside its valid range. */
    TERMOLE_STATUS_INVALID_ARGUMENT = 1,
    /* The object is not in a state where the operation is meaningful. */
    TERMOLE_STATUS_INVALID_STATE = 2,
    /* The caller and DLL disagree about the ABI/options layout. */
    TERMOLE_STATUS_ABI_MISMATCH = 3,
    /* A Windows call failed; termole_last_error() carries the detail. */
    TERMOLE_STATUS_SYSTEM_ERROR = 4,
    /* The requested program or default shell could not be found. */
    TERMOLE_STATUS_NOT_FOUND = 5,
    TERMOLE_STATUS_OUT_OF_MEMORY = 6,
    TERMOLE_STATUS_UNSUPPORTED = 7
} termole_status;

typedef enum termole_key {
    TERMOLE_KEY_ENTER = 1,
    TERMOLE_KEY_TAB = 2,
    TERMOLE_KEY_BACKSPACE = 3,
    TERMOLE_KEY_ESCAPE = 4,
    TERMOLE_KEY_ARROW_UP = 5,
    TERMOLE_KEY_ARROW_DOWN = 6,
    TERMOLE_KEY_ARROW_LEFT = 7,
    TERMOLE_KEY_ARROW_RIGHT = 8,
    TERMOLE_KEY_HOME = 9,
    TERMOLE_KEY_END = 10,
    TERMOLE_KEY_DELETE = 11,
    TERMOLE_KEY_INSERT = 12,
    TERMOLE_KEY_PAGE_UP = 13,
    TERMOLE_KEY_PAGE_DOWN = 14,
    TERMOLE_KEY_CTRL_C = 15,
    TERMOLE_KEY_CTRL_D = 16
} termole_key;

typedef struct termole_session_options {
    /* sizeof(termole_session_options) as compiled by the caller. */
    uint32_t size;
    uint32_t abi_version;
    /* NULL/empty selects PowerShell 7 when installed, otherwise Windows
       PowerShell. */
    const wchar_t* program;
    /* Optional command-line tail for program. It is passed through as one
       string; argument quoting semantics are refined by the CLI slice. */
    const wchar_t* arguments;
    /* NULL/empty inherits the caller's current directory. */
    const wchar_t* working_directory;
    /* 0 selects 80 x 24. */
    uint16_t columns;
    uint16_t rows;
} termole_session_options;

#define TERMOLE_SESSION_OPTIONS_INIT \
    { sizeof(termole_session_options), TERMOLE_ABI_VERSION, NULL, NULL, NULL, 0, 0 }

typedef struct termole_view_options {
    uint32_t size;
    uint32_t abi_version;
    /* Font size in DIPs. 0 selects 16. */
    uint32_t font_size_dip;
    uint32_t reserved;
} termole_view_options;

#define TERMOLE_VIEW_OPTIONS_INIT \
    { sizeof(termole_view_options), TERMOLE_ABI_VERSION, 0, 0 }

/* Returns TERMOLE_ABI_VERSION compiled into this DLL. */
TERMOLE_API uint32_t TERMOLE_CALL termole_abi_version(void);

/* Thread-local, NUL-terminated UTF-8 diagnostic. Never NULL. */
TERMOLE_API const char* TERMOLE_CALL termole_last_error(void);

/* Releases a string returned by this library. NULL is accepted. */
TERMOLE_API void TERMOLE_CALL termole_free(void* memory);

TERMOLE_API termole_status TERMOLE_CALL
termole_session_create(const termole_session_options* options, termole_session** out_session);

/* Releases the caller's Session handle. If no View still holds the Session,
   terminates the managed process tree and drains output before returning. If
   a View holds it, the Session stays alive until that View is destroyed; call
   termole_session_terminate to stop it immediately. */
TERMOLE_API void TERMOLE_CALL termole_session_destroy(termole_session* session);

/* Requests termination of the process tree. Idempotent. The Session remains
   queryable for its last screen until destroyed. */
TERMOLE_API termole_status TERMOLE_CALL termole_session_terminate(termole_session* session);

/* Resizes both the character grid and the ConPTY. */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_resize(termole_session* session, uint16_t columns, uint16_t rows);

/* Writes raw bytes to the terminal input channel. */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_write(termole_session* session, const void* data, size_t size);

/* Encodes one terminal key using the current basic terminal conventions. */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_send_key(termole_session* session, termole_key key);

TERMOLE_API termole_status TERMOLE_CALL
termole_session_get_size(termole_session* session, uint16_t* out_columns, uint16_t* out_rows);

/* 1 while the child process (or its output pipe) is still alive, 0 otherwise. */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_is_running(termole_session* session, int* out_running);

/* Visible screen snapshot as UTF-8, one line per row, trailing spaces removed
   and trailing empty rows omitted. Library allocated; free with
   termole_free(). */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_get_screen_text(termole_session* session, char** out_text, size_t* out_size);

/* Current OSC window title as UTF-8. Library allocated. */
TERMOLE_API termole_status TERMOLE_CALL
termole_session_get_title(termole_session* session, char** out_text, size_t* out_size);

/* Creates a child HWND under parent. The caller owns layout, visibility and
   focus; destroying the View does not terminate the Session. */
TERMOLE_API termole_status TERMOLE_CALL
termole_view_create(termole_session* session, HWND parent, const termole_view_options* options,
                    termole_view** out_view);

TERMOLE_API void TERMOLE_CALL termole_view_destroy(termole_view* view);

/* Borrowed child window handle. Returns NULL after the View is destroyed. */
TERMOLE_API HWND TERMOLE_CALL termole_view_hwnd(termole_view* view);

/* Sets keyboard focus to the child window. */
TERMOLE_API termole_status TERMOLE_CALL termole_view_focus(termole_view* view);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TERMOLE_TERMOLE_H */
