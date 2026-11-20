#include <termole/script.h>

#include <stdio.h>

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
    check(termole_script_create(&options, &script) == TERMOLE_STATUS_UNSUPPORTED,
          "default build reports QuickJS unsupported");
    check(script == NULL, "unsupported create clears out");

    check(termole_script_eval(NULL, "1", 1, NULL) == TERMOLE_STATUS_INVALID_ARGUMENT,
          "null script rejected");
    termole_script_destroy(NULL);

    if (failures != 0) {
        fprintf(stderr, "%d script check(s) failed\n", failures);
        return 1;
    }
    printf("PASS: script ABI stub\n");
    return 0;
}
