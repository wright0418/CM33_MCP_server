#include "mcp_types.h"

#include <stdio.h>

#include "cJSON.h"
#include "nualink_board.h"

static const char s_led_set_schema[] =
    "{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}},\"required\":[\"on\"],\"additionalProperties\":false}";

static int32_t prvLedSetCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *on;
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[48];
    bool led_on;

    (void)context;

    if((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    on = cJSON_GetObjectItemCaseSensitive(arguments, "on");
    if((on == NULL) || !cJSON_IsBool(on))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    led_on = cJSON_IsTrue(on) ? true : false;
    NuAILink_BoardSetLed(led_on);

    content = cJSON_CreateArray();
    text_item = cJSON_CreateObject();
    structured = cJSON_CreateObject();
    if((content == NULL) || (text_item == NULL) || (structured == NULL))
    {
        cJSON_Delete(content);
        cJSON_Delete(text_item);
        cJSON_Delete(structured);
        return MCP_STATUS_INTERNAL_ERROR;
    }

    (void)snprintf(message, sizeof(message), "PC14 LED is %s", led_on ? "on" : "off");
    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);
    (void)cJSON_AddBoolToObject(structured, "on", led_on ? 1 : 0);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

const mcp_tool_t gNuAILinkLedSetTool =
{
    "led.set",
    "Set the NuMaker PC14 active-low LED and stop the automatic heartbeat blink.",
    s_led_set_schema,
    prvLedSetCallback,
    NULL
};