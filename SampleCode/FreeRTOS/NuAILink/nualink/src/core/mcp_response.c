#include "mcp_response.h"

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

static void prvAddJsonRpcId(cJSON *response, const cJSON *id)
{
    cJSON *id_copy;

    if(id == NULL)
    {
        (void)cJSON_AddNullToObject(response, "id");
        return;
    }

    id_copy = cJSON_Duplicate(id, 1);
    if(id_copy != NULL)
    {
        (void)cJSON_AddItemToObject(response, "id", id_copy);
    }
    else
    {
        (void)cJSON_AddNullToObject(response, "id");
    }
}

int32_t MCP_ResponseSerialize(cJSON *response, char *buffer, size_t buffer_size)
{
    size_t length;

    if((response == NULL) || (buffer == NULL) || (buffer_size < 8U))
    {
        return -1;
    }

    buffer[0] = '\0';
    if(cJSON_PrintPreallocated(response, buffer, (int)(buffer_size - 1U), 0) == 0)
    {
        return -2;
    }

    length = strlen(buffer);
    if((length + 2U) > buffer_size)
    {
        return -2;
    }

    buffer[length] = '\n';
    buffer[length + 1U] = '\0';
    return (int32_t)(length + 1U);
}

int32_t MCP_ResponseWriteError(char *buffer, size_t buffer_size, const cJSON *id, int32_t code, const char *message)
{
    cJSON *response;
    cJSON *error;
    int32_t result;

    response = cJSON_CreateObject();
    error = cJSON_CreateObject();
    if((response == NULL) || (error == NULL))
    {
        cJSON_Delete(response);
        cJSON_Delete(error);
        (void)snprintf(buffer, buffer_size, "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32603,\"message\":\"internal_error\"},\"id\":null}\n");
        return (int32_t)strlen(buffer);
    }

    (void)cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    (void)cJSON_AddNumberToObject(error, "code", (double)code);
    (void)cJSON_AddStringToObject(error, "message", message);
    (void)cJSON_AddItemToObject(response, "error", error);
    prvAddJsonRpcId(response, id);

    result = MCP_ResponseSerialize(response, buffer, buffer_size);
    cJSON_Delete(response);
    return result;
}

int32_t MCP_ResponseWriteSuccess(char *buffer, size_t buffer_size, const cJSON *id, cJSON *result_object)
{
    cJSON *response;
    int32_t result;

    response = cJSON_CreateObject();
    if((response == NULL) || (result_object == NULL))
    {
        cJSON_Delete(response);
        cJSON_Delete(result_object);
        return MCP_ResponseWriteError(buffer, buffer_size, id, MCP_STATUS_INTERNAL_ERROR, "internal_error");
    }

    (void)cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    (void)cJSON_AddItemToObject(response, "result", result_object);
    prvAddJsonRpcId(response, id);

    result = MCP_ResponseSerialize(response, buffer, buffer_size);
    cJSON_Delete(response);
    return result;
}