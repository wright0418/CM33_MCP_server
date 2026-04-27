#include "mcp_types.h"

#include <stdio.h>

#include "FreeRTOS.h"
#include "task.h"

#include "cJSON.h"
#include "nualink_board.h"
#include "nualink_config.h"
#include "nualink_transport.h"

static const char s_system_info_schema[] =
    "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

static int32_t prvSystemInfoCallback(const cJSON *arguments, cJSON *result, void *context)
{
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[96];

    (void)arguments;
    (void)context;

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

    (void)snprintf(message,
                   sizeof(message),
                   "NuAILink %s, HCLK %lu Hz, tick %lu",
                   NUALINK_FIRMWARE_VERSION,
                   (unsigned long)NuAILink_BoardGetCoreClockHz(),
                   (unsigned long)xTaskGetTickCount());

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "firmwareVersion", NUALINK_FIRMWARE_VERSION);
    (void)cJSON_AddNumberToObject(structured, "hclkHz", (double)NuAILink_BoardGetCoreClockHz());
    (void)cJSON_AddNumberToObject(structured, "tick", (double)xTaskGetTickCount());
    (void)cJSON_AddNumberToObject(structured, "freeHeapBytes", (double)xPortGetFreeHeapSize());
    (void)cJSON_AddNumberToObject(structured, "minimumEverFreeHeapBytes", (double)xPortGetMinimumEverFreeHeapSize());
    (void)cJSON_AddNumberToObject(structured, "usbRxDropCount", (double)NuAILink_TransportGetRxDropCount());
    (void)cJSON_AddBoolToObject(structured, "usbAttached", NuAILink_TransportIsAttached() ? 1 : 0);
    (void)cJSON_AddBoolToObject(structured, "ledOn", NuAILink_BoardIsLedOn() ? 1 : 0);
    (void)cJSON_AddStringToObject(structured,
                                  "ledMode",
                                  NuAILink_BoardIsLedBpwmActive() ? "bpwm" : "gpio");
    (void)cJSON_AddNumberToObject(structured,
                                  "ledDutyPercent",
                                  (double)NuAILink_BoardGetLedBpwmDutyPercent());

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

const mcp_tool_t gNuAILinkSystemInfoTool =
{
    "system.info",
    "Report NuAILink firmware, clock, tick, heap, USB, and LED status.",
    s_system_info_schema,
    prvSystemInfoCallback,
    NULL
};