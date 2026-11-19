#include <termole/script.h>

#include <stddef.h>

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
    return TERMOLE_STATUS_UNSUPPORTED;
}

void TERMOLE_CALL termole_script_destroy(termole_script* script)
{
    (void)script;
}

termole_status TERMOLE_CALL termole_script_eval(termole_script* script, const char* source,
                                                size_t length, char** out_error)
{
    if (!script || (!source && length != 0)) return TERMOLE_STATUS_INVALID_ARGUMENT;
    if (out_error) *out_error = NULL;
    return TERMOLE_STATUS_UNSUPPORTED;
}
