#ifndef MCP_TYPES_H
#define MCP_TYPES_H

#include <stdint.h>

#include "cJSON.h"

#define MCP_STATUS_OK                 0
#define MCP_STATUS_INVALID_PARAMS    -32602
#define MCP_STATUS_INTERNAL_ERROR    -32603

typedef int32_t (*mcp_tool_callback_t)(const cJSON *arguments, cJSON *result, void *context);

typedef struct
{
    const char *name;
    const char *description;
    const char *input_schema_json;
    mcp_tool_callback_t callback;
    void *context;
} mcp_tool_t;

#endif /* MCP_TYPES_H */