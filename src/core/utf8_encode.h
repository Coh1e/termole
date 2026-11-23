#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes one to four bytes without a terminator. The caller supplies the
 * invalid-code-point policy and a buffer of at least four bytes. */
size_t termole_utf8_encode(uint32_t codepoint, char* output);

#ifdef __cplusplus
}
#endif
