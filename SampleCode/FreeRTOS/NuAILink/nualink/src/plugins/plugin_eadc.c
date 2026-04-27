/*
 * NuAILink Phase 2.3: EADC single-channel read helper.
 *
 * Tool provided:
 *   eadc.read {channel:8|9} -> {channel,pin,raw,mV}
 *   eadc.auto {action,start/update args...} -> periodic sampling cache
 */

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "FreeRTOS.h"
#include "nualink_board.h"
#include "task.h"

#define NUALINK_EADC_AUTO_DEFAULT_CHANNEL 8U
#define NUALINK_EADC_AUTO_DEFAULT_INTERVAL_MS 200U
#define NUALINK_EADC_AUTO_MIN_INTERVAL_MS 20U
#define NUALINK_EADC_AUTO_MAX_INTERVAL_MS 10000U

static const char s_eadc_read_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"channel\":{\"type\":\"integer\",\"enum\":[8,9]}"
    "},\"required\":[\"channel\"],\"additionalProperties\":false}";

static const char s_eadc_auto_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"update\",\"stop\",\"status\"]},"
    "\"channel\":{\"type\":\"integer\",\"enum\":[8,9]},"
    "\"interval_ms\":{\"type\":\"integer\",\"minimum\":20,\"maximum\":10000}"
    "},\"required\":[\"action\"],\"additionalProperties\":false}";

typedef struct
{
    bool enabled;
    bool sample_valid;
    uint32_t channel;
    uint32_t interval_ms;
    uint32_t raw;
    uint32_t millivolt;
    TickType_t next_tick;
    TickType_t last_sample_tick;
} nualink_eadc_auto_state_t;

static nualink_eadc_auto_state_t s_eadc_auto =
    {
        false,
        false,
        NUALINK_EADC_AUTO_DEFAULT_CHANNEL,
        NUALINK_EADC_AUTO_DEFAULT_INTERVAL_MS,
        0U,
        0U,
        0U,
        0U};

static TickType_t prvMsToTicksMin1(uint32_t interval_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(interval_ms);

    if (ticks == 0U)
    {
        ticks = 1U;
    }

    return ticks;
}

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

static bool prvSampleEadcAuto(void)
{
    uint32_t raw;
    uint32_t millivolt;

    if (!NuAILink_BoardEadcRead(s_eadc_auto.channel, &raw, &millivolt))
    {
        return false;
    }

    s_eadc_auto.raw = raw;
    s_eadc_auto.millivolt = millivolt;
    s_eadc_auto.last_sample_tick = xTaskGetTickCount();
    s_eadc_auto.sample_valid = true;
    return true;
}

static int32_t prvBuildEadcAutoResult(cJSON *result, const char *action)
{
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    const char *pin_name = prvPinNameFromChannel(s_eadc_auto.channel);
    char message[128];

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
                   "EADC auto %s: running=%lu CH%lu(%s) interval_ms=%lu valid=%lu raw=%lu mV=%lu",
                   action,
                   (unsigned long)(s_eadc_auto.enabled ? 1U : 0U),
                   (unsigned long)s_eadc_auto.channel,
                   pin_name,
                   (unsigned long)s_eadc_auto.interval_ms,
                   (unsigned long)(s_eadc_auto.sample_valid ? 1U : 0U),
                   (unsigned long)s_eadc_auto.raw,
                   (unsigned long)s_eadc_auto.millivolt);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "action", action);
    (void)cJSON_AddBoolToObject(structured, "running", s_eadc_auto.enabled ? 1 : 0);
    (void)cJSON_AddBoolToObject(structured, "sample_valid", s_eadc_auto.sample_valid ? 1 : 0);
    (void)cJSON_AddNumberToObject(structured, "channel", (double)s_eadc_auto.channel);
    (void)cJSON_AddStringToObject(structured, "pin", pin_name);
    (void)cJSON_AddNumberToObject(structured, "interval_ms", (double)s_eadc_auto.interval_ms);
    (void)cJSON_AddNumberToObject(structured, "raw", (double)s_eadc_auto.raw);
    (void)cJSON_AddNumberToObject(structured, "mV", (double)s_eadc_auto.millivolt);
    (void)cJSON_AddNumberToObject(structured, "last_sample_tick", (double)s_eadc_auto.last_sample_tick);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);
    return MCP_STATUS_OK;
}

static int32_t prvEadcAutoCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *action_item;
    const char *action;
    uint32_t channel = s_eadc_auto.channel;
    uint32_t interval_ms = s_eadc_auto.interval_ms;

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    action_item = cJSON_GetObjectItemCaseSensitive(arguments, "action");
    if ((action_item == NULL) || !cJSON_IsString(action_item) || (action_item->valuestring == NULL))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    action = action_item->valuestring;

    if ((strcmp(action, "start") == 0) || (strcmp(action, "update") == 0))
    {
        if (strcmp(action, "start") == 0)
        {
            channel = NUALINK_EADC_AUTO_DEFAULT_CHANNEL;
            interval_ms = NUALINK_EADC_AUTO_DEFAULT_INTERVAL_MS;
        }

        if (!prvGetU32InRange(arguments, "channel", 8U, 9U, &channel, false) ||
            ((channel != 8U) && (channel != 9U)) ||
            !prvGetU32InRange(arguments,
                              "interval_ms",
                              NUALINK_EADC_AUTO_MIN_INTERVAL_MS,
                              NUALINK_EADC_AUTO_MAX_INTERVAL_MS,
                              &interval_ms,
                              false))
        {
            return MCP_STATUS_INVALID_PARAMS;
        }

        s_eadc_auto.channel = channel;
        s_eadc_auto.interval_ms = interval_ms;

        if (strcmp(action, "start") == 0)
        {
            s_eadc_auto.enabled = true;
            s_eadc_auto.sample_valid = false;
        }

        if (s_eadc_auto.enabled)
        {
            if (!prvSampleEadcAuto())
            {
                s_eadc_auto.enabled = false;
                return MCP_STATUS_INTERNAL_ERROR;
            }
            s_eadc_auto.next_tick = xTaskGetTickCount() + prvMsToTicksMin1(s_eadc_auto.interval_ms);
        }
    }
    else if (strcmp(action, "stop") == 0)
    {
        s_eadc_auto.enabled = false;
    }
    else if (strcmp(action, "status") != 0)
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    return prvBuildEadcAutoResult(result, action);
}

void NuAILink_EadcAutoProcess(void)
{
    TickType_t now;

    if (!s_eadc_auto.enabled)
    {
        return;
    }

    now = xTaskGetTickCount();
    if ((int32_t)(now - s_eadc_auto.next_tick) < 0)
    {
        return;
    }

    if (!prvSampleEadcAuto())
    {
        s_eadc_auto.enabled = false;
        return;
    }

    s_eadc_auto.next_tick = now + prvMsToTicksMin1(s_eadc_auto.interval_ms);
}

const mcp_tool_t gNuAILinkEadcReadTool =
    {
        "eadc.read",
        "Read EADC single-ended channel 8 (PB8) or 9 (PB9).",
        s_eadc_read_schema,
        prvEadcReadCallback,
        NULL};

const mcp_tool_t gNuAILinkEadcAutoTool =
    {
        "eadc.auto",
        "Control autonomous EADC periodic sampling cache (start/update/stop/status).",
        s_eadc_auto_schema,
        prvEadcAutoCallback,
        NULL};
