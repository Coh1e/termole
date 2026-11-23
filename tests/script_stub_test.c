#include <termole/script.h>
#include "script/plugin.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char* what)
{
    if (condition) printf("PASS: %s\n", what);
    else {
        fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

#ifdef TERMOLE_WITH_QUICKJS
static void check_plugin(const char* path)
{
    FILE* file = fopen(path, "rb");
    check(file != NULL, "example plugin exists");
    if (!file) return;
    char source[4096];
    const size_t length = fread(source, 1, sizeof(source), file);
    const int complete = feof(file) && !ferror(file);
    fclose(file);
    check(complete, "read complete example plugin");
    if (!complete) return;

    termole_script* plugin = NULL;
    char* error = NULL;
    check(termole_plugin_load(path, source, length, &plugin, &error) == TERMOLE_STATUS_OK &&
              plugin && !error,
          "example registers a command");
    termole_free(error);
    if (!plugin) return;
    check(strcmp(termole_plugin_label(plugin), "复制为 Markdown 引用") == 0,
          "example menu label matches the specification");
    const char selection[] = "第一行     \nsecond line";
    char* clipboard = NULL;
    check(termole_plugin_invoke(plugin, selection, sizeof(selection) - 1, &clipboard, &error) ==
              TERMOLE_STATUS_OK && clipboard &&
              strcmp(clipboard, "> 第一行\r\n> second line") == 0,
          "quote the selected lines without trailing grid padding");
    termole_free(clipboard);
    termole_free(error);

    const char other_selection[] = "窗口 B";
    check(termole_plugin_invoke(plugin, other_selection, sizeof(other_selection) - 1,
                                &clipboard, &error) == TERMOLE_STATUS_OK &&
              clipboard && strcmp(clipboard, "> 窗口 B") == 0,
          "use the selection supplied for each invocation");
    termole_free(clipboard);
    termole_free(error);
    termole_script_destroy(plugin);

    const char invalid[] = "termole.registerCommand('Copy', () => {}); throw Error('failed');";
    check(termole_plugin_load("invalid", invalid, sizeof(invalid) - 1, &plugin, &error) ==
              TERMOLE_STATUS_SYSTEM_ERROR && !plugin && error,
          "discard a registered command when loading fails");
    termole_free(error);

    const char failing[] =
        "termole.registerCommand('Copy', () => {"
        "termole.writeClipboard('partial'); throw Error('failed'); });";
    check(termole_plugin_load("failing", failing, sizeof(failing) - 1, &plugin, &error) ==
              TERMOLE_STATUS_OK && plugin && !error,
          "register a command that can fail when invoked");
    termole_free(error);
    if (!plugin) return;
    check(termole_plugin_invoke(plugin, "text", 4, &clipboard, &error) ==
              TERMOLE_STATUS_SYSTEM_ERROR && !clipboard && error,
          "discard staged clipboard text when the command fails");
    termole_free(error);
    termole_script_destroy(plugin);

    const char async[] =
        "termole.registerCommand('Copy', async () => {"
        "termole.writeClipboard('partial'); throw Error('failed'); });";
    check(termole_plugin_load("async", async, sizeof(async) - 1, &plugin, &error) ==
              TERMOLE_STATUS_OK && plugin && !error,
          "load a plugin with an asynchronous command");
    termole_free(error);
    if (!plugin) return;
    check(termole_plugin_invoke(plugin, "text", 4, &clipboard, &error) ==
              TERMOLE_STATUS_SYSTEM_ERROR && !clipboard && error,
          "reject an asynchronous command without committing staged text");
    termole_free(error);
    termole_script_destroy(plugin);
}
#endif

int main(int argc, char** argv)
{
    termole_script* script = (termole_script*)1;
    check(termole_script_create(NULL, &script) == TERMOLE_STATUS_INVALID_ARGUMENT &&
              script == NULL,
          "null options rejected and out cleared");

    termole_script_options options = TERMOLE_SCRIPT_OPTIONS_INIT;
    options.abi_version = 999;
    check(termole_script_create(&options, &script) == TERMOLE_STATUS_ABI_MISMATCH,
          "wrong script ABI rejected");

    options = (termole_script_options)TERMOLE_SCRIPT_OPTIONS_INIT;
#ifdef TERMOLE_WITH_QUICKJS
    check(termole_script_create(&options, &script) == TERMOLE_STATUS_OK && script != NULL,
          "enabled build creates a script context");
    if (!script) return 1;
    char* error = NULL;
    const char source[] = { '4', '2' };
    check(termole_script_eval(script, source, sizeof(source), &error) == TERMOLE_STATUS_OK &&
              error == NULL,
          "evaluate a source range without a NUL terminator");
    const char exception[] = "throw new Error('termole-script-test')";
    check(termole_script_eval(script, exception, sizeof(exception) - 1, &error) ==
              TERMOLE_STATUS_SYSTEM_ERROR && error && strstr(error, "termole-script-test"),
          "return an owned diagnostic for a script exception");
    termole_free(error);
    error = NULL;

    const char isolated[] = "if (typeof std !== 'undefined' || typeof os !== 'undefined') throw 1;";
    check(termole_script_eval(script, isolated, sizeof(isolated) - 1, &error) ==
              TERMOLE_STATUS_OK && error == NULL,
          "no host filesystem or process APIs are installed");
    const char loop[] = "while (true) {}";
    check(termole_script_eval(script, loop, sizeof(loop) - 1, &error) ==
              TERMOLE_STATUS_SYSTEM_ERROR && error,
          "an infinite loop is interrupted");
    termole_free(error);
    error = NULL;
    check(termole_script_eval(script, source, sizeof(source), &error) == TERMOLE_STATUS_OK &&
              error == NULL,
          "context remains usable after interruption");
    termole_script_destroy(script);
    if (argc == 2) check_plugin(argv[1]);
    else check(0, "example script path supplied");
#else
    (void)argc;
    (void)argv;
    check(termole_script_create(&options, &script) == TERMOLE_STATUS_UNSUPPORTED,
          "default build reports QuickJS unsupported");
    check(script == NULL, "unsupported create clears out");
    check(termole_plugin_load("disabled", "", 0, &script, NULL) == TERMOLE_STATUS_UNSUPPORTED &&
              !script,
          "disabled build does not load a plugin");
#endif

    check(termole_script_eval(NULL, "1", 1, NULL) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "null script rejected");
    termole_script_destroy(NULL);

    if (failures != 0) {
        fprintf(stderr, "%d script check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: script ABI\n");
    return 0;
}
