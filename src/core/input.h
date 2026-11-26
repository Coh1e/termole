#pragma once

#include <termole/core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns static, NUL-terminated bytes, or NULL for an unknown key. */
const char* termole_key_sequence(termole_key key, int application_cursor);

/* Wraps length bytes in place, preserving the payload verbatim. The caller
 * provides room for both delimiters. The result has no trailing NUL. */
#define TERMOLE_PASTE_OVERHEAD 12u
void termole_bracket_paste(char* bytes, size_t length);

#ifdef __cplusplus
}

#include <string>
namespace termole {
inline void bracket_paste(std::string& bytes)
{
    const size_t length = bytes.size();
    bytes.append(TERMOLE_PASTE_OVERHEAD, '\0');
    termole_bracket_paste(bytes.data(), length);
}
}
#endif
