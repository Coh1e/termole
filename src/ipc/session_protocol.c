#include "ipc/session_protocol.h"

uint32_t termole_ipc_load_u32(const uint8_t bytes[4])
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
        ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

void termole_ipc_store_u32(uint8_t bytes[4], uint32_t value)
{
    for (unsigned index = 0; index < 4; ++index)
        bytes[index] = (uint8_t)(value >> (8 * index));
}

bool termole_ipc_read_header(const uint8_t bytes[TERMOLE_IPC_HEADER_SIZE],
                             termole_ipc_header* header)
{
    header->size = termole_ipc_load_u32(bytes);
    header->type = bytes[4];
    header->version = bytes[5];
    return !bytes[6] && !bytes[7] && header->size <= TERMOLE_IPC_MAX_PAYLOAD;
}

void termole_ipc_write_header(uint8_t bytes[TERMOLE_IPC_HEADER_SIZE],
                              termole_ipc_header header)
{
    termole_ipc_store_u32(bytes, header.size);
    bytes[4] = header.type;
    bytes[5] = header.version;
    bytes[6] = 0;
    bytes[7] = 0;
}
