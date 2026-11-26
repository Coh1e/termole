#pragma once

#include <termole/termole.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Zero-initialize before the first read. The caller owns frame.cells and
   releases it with termole_free. A failed read preserves the previous frame. */
typedef struct termole_viewport {
    termole_frame frame;
    size_t capacity;
    int32_t first_document_row;
    int32_t document_rows;
} termole_viewport;

/* Copies a single session revision. The caller may paint and mark selected
   cells after this returns, independently of subsequent output or resizes. */
termole_status termole_session_read_viewport(termole_session* session, int32_t scroll_offset,
                                            termole_viewport* out);

#ifdef __cplusplus
}
#endif
