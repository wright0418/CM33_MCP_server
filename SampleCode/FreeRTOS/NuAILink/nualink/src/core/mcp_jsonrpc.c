#include "mcp_jsonrpc.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "cJSON.h"
#include "mcp_registry.h"
#include "mcp_response.h"
#include "mcp_types.h"
#include "nualink_config.h"

static bool prvHasResponseId(const cJSON *root)
{
    return cJSON_GetObjectItemCaseSensitive(root, "id") != NULL;
}

static cJSON *prvCreateInitializeResult(void)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *tools = cJSON_CreateObject();
    cJSON *server_info = cJSON_CreateObject();

    if((result == NULL) || (capabilities == NULL) || (tools == NULL) || (server_info == NULL))
    {
        cJSON_Delete(result);
        cJSON_Delete(capabilities);
        cJSON_Delete(tools);
        cJSON_Delete(server_info);
        return NULL;
    }

    (void)cJSON_AddStringToObject(result, "protocolVersion", NUALINK_PROTOCOL_VERSION);
    (void)cJSON_AddStringToObject(server_info, "name", "NuAILink");
    (void)cJSON_AddStringToObject(server_info, "version", NUALINK_FIRMWARE_VERSION);
    (void)cJSON_AddItemToObject(capabilities, "tools", tools);
    (void)cJSON_AddItemToObject(result, "capabilities", capabilities);
    (void)cJSON_AddItemToObject(result, "serverInfo", server_info);

    return result;
}

static cJSON *prvCreatePingResult(void)
{
    cJSON *result = cJSON_CreateObject();

    if(result != NULL)
    {
        (void)cJSON_AddStringToObject(result, "status", "ok");
        (void)cJSON_AddNumberToObject(result, "tick", (double)xTaskGetTickCount());
    }

    return result;
}

static cJSON *prvCreateToolsListResult(void)
{
    cJSON *result = cJSON_CreateObject();
    cJSON *tools_array = cJSON_CreateArray();
    uint32_t index;

    if((result == NULL) || (tools_array == NULL))
    {
        cJSON_Delete(result);
        cJSON_Delete(tools_array);
        return NULL;
    }

    for(index = 0U; index < MCP_RegistryCount(); index++)
    {
        const mcp_tool_t *tool = MCP_RegistryGet(index);
        cJSON *tool_object = cJSON_CreateObject();
        cJSON *schema = NULL;

        if((tool == NULL) || (tool_object == NULL))
        {
            cJSON_Delete(tool_object);
            cJSON_Delete(result);
            return NULL;
        }

        (void)cJSON_AddStringToObject(tool_object, "name", tool->name);
        (void)cJSON_AddStringToObject(tool_object, "description", tool->description);
        schema = cJSON_CreateRaw(tool->input_schema_json);
        if(schema == NULL)
        {
            cJSON_Delete(tool_object);
            cJSON_Delete(result);
            return NULL;
        }
        (void)cJSON_AddItemToObject(tool_object, "inputSchema", schema);
        (void)cJSON_AddItemToArray(tools_array, tool_object);
    }

    (void)cJSON_AddItemToObject(result, "tools", tools_array);
    return result;
}

static int32_t prvHandleToolCall(const cJSON *params, cJSON **result_out)
{
    const cJSON *name;
    const cJSON *arguments;
    const mcp_tool_t *tool;
    cJSON *tool_result;
    int32_t status;

    if((params == NULL) || !cJSON_IsObject(params))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    name = cJSON_GetObjectItemCaseSensitive(params, "name");
    arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
    if((name == NULL) || !cJSON_IsString(name) || (name->valuestring == NULL))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    if((arguments != NULL) && !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    tool = MCP_RegistryFind(name->valuestring);
    if(tool == NULL)
    {
        return -32601;
    }

    tool_result = cJSON_CreateObject();
    if(tool_result == NULL)
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }

    status = tool->callback(arguments, tool_result, tool->context);
    if(status != MCP_STATUS_OK)
    {
        cJSON_Delete(tool_result);
        return status;
    }

    *result_out = tool_result;
    return MCP_STATUS_OK;
}

int32_t MCP_JSONRPC_Handle(const char *request, char *response, size_t response_size)
{
    cJSON *root;
    const cJSON *id;
    const cJSON *method;
    bool has_response_id;
    cJSON *result = NULL;
    int32_t status = MCP_STATUS_OK;

    if((request == NULL) || (response == NULL))
    {
        return -1;
    }

    root = cJSON_Parse(request);
    if((root == NULL) || !cJSON_IsObject(root))
    {
        cJSON_Delete(root);
        return MCP_ResponseWriteError(response, response_size, NULL, -32700, "parse_error");
    }

    id = cJSON_GetObjectItemCaseSensitive(root, "id");
    has_response_id = prvHasResponseId(root);
    method = cJSON_GetObjectItemCaseSensitive(root, "method");

    if((method == NULL) || !cJSON_IsString(method) || (method->valuestring == NULL))
    {
        status = -32600;
    }
    else if(strcmp(method->valuestring, "initialize") == 0)
    {
        result = prvCreateInitializeResult();
    }
    else if(strcmp(method->valuestring, "ping") == 0)
    {
        result = prvCreatePingResult();
    }
    else if(strcmp(method->valuestring, "tools/list") == 0)
    {
        result = prvCreateToolsListResult();
    }
    else if(strcmp(method->valuestring, "tools/call") == 0)
    {
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        status = prvHandleToolCall(params, &result);
    }
    else
    {
        status = -32601;
    }

    if(!has_response_id && (status == MCP_STATUS_OK))
    {
        cJSON_Delete(result);
        cJSON_Delete(root);
        return 0;
    }

    if(status != MCP_STATUS_OK)
    {
        const char *message = "internal_error";
        int32_t response_length;

        if(status == -32600)
        {
            message = "invalid_request";
        }
        else if(status == -32601)
        {
            message = "method_not_found";
        }
        else if(status == MCP_STATUS_INVALID_PARAMS)
        {
            message = "invalid_params";
        }

        response_length = MCP_ResponseWriteError(response, response_size, id, status, message);
        cJSON_Delete(root);
        return response_length;
    }

    if(result == NULL)
    {
        int32_t response_length;

        response_length = MCP_ResponseWriteError(response, response_size, id, MCP_STATUS_INTERNAL_ERROR, "internal_error");
        cJSON_Delete(root);
        return response_length;
    }

    status = MCP_ResponseWriteSuccess(response, response_size, id, result);
    if(status < 0)
    {
        status = MCP_ResponseWriteError(response,
                                        response_size,
                                        id,
                                        MCP_STATUS_INTERNAL_ERROR,
                                        "response_too_large");
    }
    cJSON_Delete(root);
    return status;
}