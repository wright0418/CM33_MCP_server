#ifndef MCP_JSONRPC_H
#define MCP_JSONRPC_H

#include <stddef.h>
#include <stdint.h>

int32_t MCP_JSONRPC_Handle(const char *request, char *response, size_t response_size);

#endif /* MCP_JSONRPC_H */