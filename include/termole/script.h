#pragma once

#include <termole/termole.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TERMOLE_SCRIPT_ABI_VERSION 1u

typedef struct termole_script termole_script;

typedef struct termole_script_options {
    size_t size;
    uint32_t abi_version;
    const char* name;
} termole_script_options;

#define TERMOLE_SCRIPT_OPTIONS_INIT \
    { sizeof(termole_script_options), TERMOLE_SCRIPT_ABI_VERSION, NULL }

/* Creates an optional script engine. The default build (QuickJS disabled)
   returns TERMOLE_STATUS_UNSUPPORTED without allocating. */
TERMOLE_API termole_status TERMOLE_CALL
termole_script_create(const termole_script_options* options, termole_script** out_script);
void TERMOLE_CALL termole_script_destroy(termole_script* script);

/* Evaluates UTF-8 source. On failure out_error is library allocated and must be
   released with termole_free(). */
TERMOLE_API termole_status TERMOLE_CALL
termole_script_eval(termole_script* script, const char* source, size_t length, char** out_error);

#ifdef __cplusplus
}
#endif
