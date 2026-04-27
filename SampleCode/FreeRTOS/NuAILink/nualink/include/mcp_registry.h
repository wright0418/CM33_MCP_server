#ifndef MCP_REGISTRY_H
#define MCP_REGISTRY_H

#include <stdint.h>

#include "mcp_types.h"

uint32_t MCP_RegistryCount(void);
const mcp_tool_t *MCP_RegistryGet(uint32_t index);
const mcp_tool_t *MCP_RegistryFind(const char *name);

#endif /* MCP_REGISTRY_H */