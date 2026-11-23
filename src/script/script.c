#include <termole/script.h>
#include "script/plugin.h"

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
    char* label;
    JSValue command;
    const char* selection;
    size_t selection_length;
    char* clipboard;
    bool loading;
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

static JSValue register_command(JSContext* context, JSValueConst receiver, int argc,
                                JSValueConst* argv)
{
    (void)receiver;
    termole_script* plugin = JS_GetContextOpaque(context);
    if (!plugin->loading || plugin->label || argc != 2 ||
        !JS_IsString(argv[0]) || !JS_IsFunction(context, argv[1])) {
        return JS_ThrowTypeError(context, "expected one command with a label and callback");
    }
    size_t length;
    const char* label = JS_ToCStringLen(context, &length, argv[0]);
    if (!label) return JS_EXCEPTION;
    if (!length || length > 256 || memchr(label, '\0', length)) {
        JS_FreeCString(context, label);
        return JS_ThrowTypeError(context, "invalid menu label");
    }
    plugin->label = copy_bytes(label, length);
    JS_FreeCString(context, label);
    if (!plugin->label) return JS_ThrowOutOfMemory(context);
    plugin->command = JS_DupValue(context, argv[1]);
    return JS_UNDEFINED;
}

static JSValue get_selection(JSContext* context, JSValueConst receiver, int argc,
                             JSValueConst* argv)
{
    (void)receiver;
    (void)argc;
    (void)argv;
    const termole_script* plugin = JS_GetContextOpaque(context);
    if (!plugin->selection) return JS_ThrowTypeError(context, "no active View");
    return JS_NewStringLen(context, plugin->selection, plugin->selection_length);
}

static JSValue write_clipboard(JSContext* context, JSValueConst receiver, int argc,
                               JSValueConst* argv)
{
    (void)receiver;
    termole_script* plugin = JS_GetContextOpaque(context);
    if (!plugin->selection || argc != 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(context, "expected clipboard text from an active command");
    }
    size_t length;
    const char* text = JS_ToCStringLen(context, &length, argv[0]);
    if (!text) return JS_EXCEPTION;
    if (memchr(text, '\0', length)) {
        JS_FreeCString(context, text);
        return JS_ThrowTypeError(context, "clipboard text contains a NUL byte");
    }
    char* copy = copy_bytes(text, length);
    JS_FreeCString(context, text);
    if (!copy) return JS_ThrowOutOfMemory(context);
    free(plugin->clipboard);
    plugin->clipboard = copy;
    return JS_UNDEFINED;
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
    if (script->context) {
        if (script->label) JS_FreeValue(script->context, script->command);
        JS_FreeContext(script->context);
    }
    if (script->runtime) JS_FreeRuntime(script->runtime);
    free(script->name);
    free(script->label);
    free(script->clipboard);
    free(script);
#else
    (void)script;
#endif
}

#ifdef TERMOLE_WITH_QUICKJS
static termole_status result_status(termole_script* script, JSValue result, char** out_error)
{
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
}
#endif

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
    return result_status(script, result, out_error);
#else
    return TERMOLE_STATUS_UNSUPPORTED;
#endif
}

termole_status termole_plugin_load(const char* name, const char* source, size_t length,
                                  termole_script** out_plugin, char** out_error)
{
    if (out_error) *out_error = NULL;
    termole_script_options options = TERMOLE_SCRIPT_OPTIONS_INIT;
    options.name = name;
    termole_status status = termole_script_create(&options, out_plugin);
#ifdef TERMOLE_WITH_QUICKJS
    if (status != TERMOLE_STATUS_OK) return status;
    termole_script* plugin = *out_plugin;
    JSContext* context = plugin->context;
    JS_SetContextOpaque(context, plugin);
    // QuickJS keeps pointers to these entries for lazy property initialization.
    static const JSCFunctionListEntry api[] = {
        JS_CFUNC_DEF("registerCommand", 2, register_command),
        JS_CFUNC_DEF("getSelection", 0, get_selection),
        JS_CFUNC_DEF("writeClipboard", 1, write_clipboard),
    };
    JSValue host = JS_NewObject(context);
    JSValue global = JS_GetGlobalObject(context);
    if (JS_IsException(host)) {
        status = result_status(plugin, host, out_error);
    } else if (JS_SetPropertyFunctionList(context, host, api, 3) < 0) {
        JS_FreeValue(context, host);
        status = result_status(plugin, JS_EXCEPTION, out_error);
    } else if (JS_SetPropertyStr(context, global, "termole", host) < 0) {
        status = result_status(plugin, JS_EXCEPTION, out_error);
    } else {
        plugin->loading = true;
        status = termole_script_eval(plugin, source, length, out_error);
        plugin->loading = false;
    }
    JS_FreeValue(context, global);
    if (status == TERMOLE_STATUS_OK && !plugin->label) {
        const char message[] = "plugin did not register a command";
        if (out_error) *out_error = copy_bytes(message, sizeof(message) - 1);
        status = TERMOLE_STATUS_INVALID_ARGUMENT;
    }
    if (status != TERMOLE_STATUS_OK) {
        termole_script_destroy(plugin);
        *out_plugin = NULL;
    }
#else
    (void)source;
    (void)length;
#endif
    return status;
}

const char* termole_plugin_label(const termole_script* plugin)
{
#ifdef TERMOLE_WITH_QUICKJS
    return plugin->label;
#else
    (void)plugin;
    return NULL;
#endif
}

termole_status termole_plugin_invoke(termole_script* plugin, const char* selection, size_t length,
                                    char** out_clipboard, char** out_error)
{
    *out_clipboard = NULL;
    if (out_error) *out_error = NULL;
#ifdef TERMOLE_WITH_QUICKJS
    plugin->selection = selection;
    plugin->selection_length = length;
    plugin->deadline = GetTickCount64() + 1000;
    JSValue result = JS_Call(plugin->context, plugin->command, JS_UNDEFINED, 0, NULL);
    if ((int)JS_PromiseState(plugin->context, result) >= 0) {
        JS_FreeValue(plugin->context, result);
        result = JS_ThrowTypeError(plugin->context, "plugin commands must be synchronous");
    }
    termole_status status = result_status(plugin, result, out_error);
    plugin->selection = NULL;
    if (status == TERMOLE_STATUS_OK) *out_clipboard = plugin->clipboard;
    else free(plugin->clipboard);
    plugin->clipboard = NULL;
    return status;
#else
    (void)plugin;
    (void)selection;
    (void)length;
    return TERMOLE_STATUS_UNSUPPORTED;
#endif
}
