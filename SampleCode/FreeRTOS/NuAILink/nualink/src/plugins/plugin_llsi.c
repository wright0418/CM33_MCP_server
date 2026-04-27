/*
 * NuAILink Phase 2.4: LLSI (WS2812) fill helper.
 *
 * Tool provided:
 *   llsi.fill {r,g,b,count?} -> fill first N pixels (default: 10)
 */

#include "mcp_types.h"

#include <stdio.h>

#include "cJSON.h"
#include "nualink_board.h"

#define NUALINK_LLSI_DEFAULT_COUNT 10U
#define NUALINK_LLSI_MAX_COUNT 10U

static const char s_llsi_fill_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"r\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"g\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"b\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10}"
    "},\"required\":[\"r\",\"g\",\"b\"],\"additionalProperties\":false}";

static bool prvGetU32InRange(const cJSON *obj,
                             const char *key,
                             uint32_t min,
                             uint32_t max,
                             uint32_t *out_value,
                             bool required)
{
    const cJSON *item;

    if ((obj == NULL) || (key == NULL) || (out_value == NULL))
    {
        return false;
    }

    item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL)
    {
        return !required;
    }
    if (!cJSON_IsNumber(item))
    {
        return false;
    }
    if ((item->valuedouble < (double)min) || (item->valuedouble > (double)max))
    {
        return false;
    }

    *out_value = (uint32_t)item->valueint;
    return true;
}

static int32_t prvLlsiFillCallback(const cJSON *arguments, cJSON *result, void *context)
{
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t count = NUALINK_LLSI_DEFAULT_COUNT;
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[96];

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    if (!prvGetU32InRange(arguments, "r", 0U, 255U, &red, true) ||
        !prvGetU32InRange(arguments, "g", 0U, 255U, &green, true) ||
        !prvGetU32InRange(arguments, "b", 0U, 255U, &blue, true) ||
        !prvGetU32InRange(arguments, "count", 1U, NUALINK_LLSI_MAX_COUNT, &count, false))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    if (!NuAILink_BoardLlsiFill(red, green, blue, count))
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }

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
                   "LLSI fill PB15: count=%lu rgb=(%lu,%lu,%lu)",
                   (unsigned long)count,
                   (unsigned long)red,
                   (unsigned long)green,
                   (unsigned long)blue);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "pin", "PB15");
    (void)cJSON_AddNumberToObject(structured, "count", (double)count);
    (void)cJSON_AddNumberToObject(structured, "r", (double)red);
    (void)cJSON_AddNumberToObject(structured, "g", (double)green);
    (void)cJSON_AddNumberToObject(structured, "b", (double)blue);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

const mcp_tool_t gNuAILinkLlsiFillTool =
    {
        "llsi.fill",
        "Fill WS2812 pixels on LLSI0(PB15) with RGB color.",
        s_llsi_fill_schema,
        prvLlsiFillCallback,
        NULL};
