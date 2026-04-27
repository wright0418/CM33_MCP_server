#ifndef MCP_RESPONSE_H
#define MCP_RESPONSE_H

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"

int32_t MCP_ResponseSerialize(cJSON *response, char *buffer, size_t buffer_size);
int32_t MCP_ResponseWriteError(char *buffer, size_t buffer_size, const cJSON *id, int32_t code, const char *message);
int32_t MCP_ResponseWriteSuccess(char *buffer, size_t buffer_size, const cJSON *id, cJSON *result);

#endif /* MCP_RESPONSE_H */