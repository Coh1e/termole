#pragma once

#include <termole/script.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use the returned plugin on the loading thread and release it with termole_script_destroy.
   The label is borrowed. Error and clipboard strings must be released with termole_free. */
termole_status termole_plugin_load(const char* name, const char* source, size_t length,
                                  termole_script** out_plugin, char** out_error);
const char* termole_plugin_label(const termole_script* plugin);
termole_status termole_plugin_invoke(termole_script* plugin, const char* selection, size_t length,
                                    char** out_clipboard, char** out_error);

#ifdef __cplusplus
}
#endif
