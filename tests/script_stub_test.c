#include <termole/script.h>

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

int main(void)
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
#else
    check(termole_script_create(&options, &script) == TERMOLE_STATUS_UNSUPPORTED,
          "default build reports QuickJS unsupported");
    check(script == NULL, "unsupported create clears out");
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
