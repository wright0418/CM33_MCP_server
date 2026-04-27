/*
 * NuAILink Phase 2.3: EADC single-channel read helper.
 *
 * Tool provided:
 *   eadc.read {channel:8|9} -> {channel,pin,raw,mV}
 */

#include "mcp_types.h"

#include <stdio.h>

#include "cJSON.h"
#include "nualink_board.h"

static const char s_eadc_read_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"channel\":{\"type\":\"integer\",\"enum\":[8,9]}"
    "},\"required\":[\"channel\"],\"additionalProperties\":false}";

static const char *prvPinNameFromChannel(uint32_t channel)
{
    if (channel == 8U)
    {
        return "PB8";
    }

    return "PB9";
}

static int32_t prvEadcReadCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *channel_item;
    uint32_t channel;
    uint32_t raw;
    uint32_t millivolt;
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    const char *pin_name;
    char message[72];

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    channel_item = cJSON_GetObjectItemCaseSensitive(arguments, "channel");
    if ((channel_item == NULL) || !cJSON_IsNumber(channel_item))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    if ((channel_item->valueint != 8) && (channel_item->valueint != 9))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    channel = (uint32_t)channel_item->valueint;

    if (!NuAILink_BoardEadcRead(channel, &raw, &millivolt))
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }

    pin_name = prvPinNameFromChannel(channel);

    content = cJSON_CreateArray();
    text_item = cJSON_CreateObject();
    structured = cJSON_CreateObject();
    if ((content == NULL) || (text_item == NULL) || (structured == NULL))
    {
        cJSON_Delete(content);
        cJSON_Delete(text_item);
        cJSON_Delete(structured);
        return MCP_STATUS_INTERNAL_ERROR;
    }

    (void)snprintf(message,
                   sizeof(message),
                   "EADC CH%lu (%s): raw=%lu, %lu mV",
                   (unsigned long)channel,
                   pin_name,
                   (unsigned long)raw,
                   (unsigned long)millivolt);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddNumberToObject(structured, "channel", (double)channel);
    (void)cJSON_AddStringToObject(structured, "pin", pin_name);
    (void)cJSON_AddNumberToObject(structured, "raw", (double)raw);
    (void)cJSON_AddNumberToObject(structured, "mV", (double)millivolt);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

const mcp_tool_t gNuAILinkEadcReadTool =
    {
        "eadc.read",
        "Read EADC single-ended channel 8 (PB8) or 9 (PB9).",
        s_eadc_read_schema,
        prvEadcReadCallback,
        NULL};
