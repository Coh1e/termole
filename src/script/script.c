#include <termole/script.h>

#include <stddef.h>

#ifdef TERMOLE_WITH_QUICKJS
#include <quickjs.h>
#include <stdlib.h>
#include <string.h>

struct termole_script {
    JSRuntime* runtime;
    JSContext* context;
    char* name;
    ULONGLONG deadline;
};

static int interrupt_eval(JSRuntime* runtime, void* opaque)
{
    (void)runtime;
    const termole_script* script = opaque;
    return GetTickCount64() >= script->deadline;
}

static char* copy_bytes(const char* bytes, size_t length)
{
    if (length == SIZE_MAX) return NULL;
    char* copy = malloc(length + 1);
    if (!copy) return NULL;
    memcpy(copy, bytes, length);
    copy[length] = '\0';
    return copy;
}
#endif

termole_status TERMOLE_CALL termole_script_create(const termole_script_options* options,
                                                  termole_script** out_script)
{
    if (!out_script) return TERMOLE_STATUS_INVALID_ARGUMENT;
    *out_script = NULL;
    if (!options) return TERMOLE_STATUS_INVALID_ARGUMENT;
    if (options->size < sizeof(termole_script_options) ||
        options->abi_version != TERMOLE_SCRIPT_ABI_VERSION) {
        return TERMOLE_STATUS_ABI_MISMATCH;
    }
#ifdef TERMOLE_WITH_QUICKJS
    termole_script* script = calloc(1, sizeof(*script));
    if (!script) return TERMOLE_STATUS_OUT_OF_MEMORY;

    const char* name = options->name ? options->name : "<termole>";
    script->name = copy_bytes(name, strlen(name));
    script->runtime = JS_NewRuntime();
    if (!script->name || !script->runtime) {
        termole_script_destroy(script);
        return TERMOLE_STATUS_OUT_OF_MEMORY;
    }
    JS_SetMemoryLimit(script->runtime, 16 * 1024 * 1024);
    JS_SetMaxStackSize(script->runtime, 256 * 1024);
    script->context = JS_NewContext(script->runtime);
    if (!script->context) {
        termole_script_destroy(script);
        return TERMOLE_STATUS_OUT_OF_MEMORY;
    }
    JS_SetInterruptHandler(script->runtime, interrupt_eval, script);
    *out_script = script;
    return TERMOLE_STATUS_OK;
#else
    return TERMOLE_STATUS_UNSUPPORTED;
#endif
}

void TERMOLE_CALL termole_script_destroy(termole_script* script)
{
#ifdef TERMOLE_WITH_QUICKJS
    if (!script) return;
    if (script->context) JS_FreeContext(script->context);
    if (script->runtime) JS_FreeRuntime(script->runtime);
    free(script->name);
    free(script);
#else
    (void)script;
#endif
}

termole_status TERMOLE_CALL termole_script_eval(termole_script* script, const char* source,
                                                size_t length, char** out_error)
{
    if (out_error) *out_error = NULL;
    if (!script || (!source && length != 0)) return TERMOLE_STATUS_INVALID_ARGUMENT;
#ifdef TERMOLE_WITH_QUICKJS
    if (length > 1024 * 1024) return TERMOLE_STATUS_INVALID_ARGUMENT;
    // QuickJS requires a NUL byte after the provided source range.
    char* input = copy_bytes(source ? source : "", length);
    if (!input) return TERMOLE_STATUS_OUT_OF_MEMORY;
    script->deadline = GetTickCount64() + 1000;
    JSValue result = JS_Eval(script->context, input, length, script->name, JS_EVAL_TYPE_GLOBAL);
    free(input);
    if (!JS_IsException(result)) {
        JS_FreeValue(script->context, result);
        return TERMOLE_STATUS_OK;
    }

    JSValue exception = JS_GetException(script->context);
    if (out_error) {
        size_t size = 0;
        const char* message = JS_ToCStringLen(script->context, &size, exception);
        if (message) {
            *out_error = copy_bytes(message, size);
            JS_FreeCString(script->context, message);
        } else {
            JSValue conversion_error = JS_GetException(script->context);
            JS_FreeValue(script->context, conversion_error);
            const char fallback[] = "QuickJS evaluation failed";
            *out_error = copy_bytes(fallback, sizeof(fallback) - 1);
        }
    }
    JS_FreeValue(script->context, exception);
    if (out_error && !*out_error) return TERMOLE_STATUS_OUT_OF_MEMORY;
    return TERMOLE_STATUS_SYSTEM_ERROR;
#else
    return TERMOLE_STATUS_UNSUPPORTED;
#endif
}
