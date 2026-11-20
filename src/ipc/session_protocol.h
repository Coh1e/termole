#ifndef TERMOLE_SESSION_PROTOCOL_H
#define TERMOLE_SESSION_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TERMOLE_IPC_HEADER_SIZE = 8,
    TERMOLE_IPC_VERSION = 1,
    TERMOLE_IPC_MAX_PAYLOAD = 48 * 1024 * 1024,
};

enum termole_ipc_type {
    TERMOLE_IPC_ATTACH = 1,
    TERMOLE_IPC_FRAME,
    TERMOLE_IPC_INPUT,
    TERMOLE_IPC_RESIZE,
    TERMOLE_IPC_MODES,
    TERMOLE_IPC_EXIT_STATUS,
    TERMOLE_IPC_OK = 0x80,
    TERMOLE_IPC_ERROR,
};

typedef struct termole_ipc_header {
    uint32_t size;
    uint8_t type;
    uint8_t version;
} termole_ipc_header;

uint32_t termole_ipc_load_u32(const uint8_t bytes[4]);
void termole_ipc_store_u32(uint8_t bytes[4], uint32_t value);
bool termole_ipc_read_header(const uint8_t bytes[TERMOLE_IPC_HEADER_SIZE],
                             termole_ipc_header* header);
void termole_ipc_write_header(uint8_t bytes[TERMOLE_IPC_HEADER_SIZE],
                              termole_ipc_header header);

#ifdef __cplusplus
}
#endif
#endif
