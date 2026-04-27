#include "mcp_registry.h"

#include <stddef.h>
#include <string.h>

extern const mcp_tool_t gNuAILinkSystemInfoTool;
extern const mcp_tool_t gNuAILinkLedSetTool;
extern const mcp_tool_t gNuAILinkLedBpwmSetTool;
extern const mcp_tool_t gNuAILinkGpioReadTool;
extern const mcp_tool_t gNuAILinkGpioWriteTool;
extern const mcp_tool_t gNuAILinkButtonReadTool;
extern const mcp_tool_t gNuAILinkEadcReadTool;

static const mcp_tool_t *const s_tools[] =
    {
        &gNuAILinkSystemInfoTool,
        &gNuAILinkLedSetTool,
        &gNuAILinkLedBpwmSetTool,
        &gNuAILinkGpioReadTool,
        &gNuAILinkGpioWriteTool,
        &gNuAILinkButtonReadTool,
        &gNuAILinkEadcReadTool};

uint32_t MCP_RegistryCount(void)
{
    return (uint32_t)(sizeof(s_tools) / sizeof(s_tools[0]));
}

const mcp_tool_t *MCP_RegistryGet(uint32_t index)
{
    if (index >= MCP_RegistryCount())
    {
        return NULL;
    }

    return s_tools[index];
}

const mcp_tool_t *MCP_RegistryFind(const char *name)
{
    uint32_t index;

    if (name == NULL)
    {
        return NULL;
    }

    for (index = 0U; index < MCP_RegistryCount(); index++)
    {
        if (strcmp(name, s_tools[index]->name) == 0)
        {
            return s_tools[index];
        }
    }

    return NULL;
}