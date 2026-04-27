#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "FreeRTOS.h"
#include "nualink_board.h"
#include "nualink_tasks.h"
#include "task.h"

#define NUALINK_LED_AUTO_DEFAULT_INTERVAL_MS 500U
#define NUALINK_LED_AUTO_MIN_INTERVAL_MS 20U
#define NUALINK_LED_AUTO_MAX_INTERVAL_MS 5000U

static const char s_led_set_schema[] =
    "{\"type\":\"object\",\"properties\":{\"on\":{\"type\":\"boolean\"}},\"required\":[\"on\"],\"additionalProperties\":false}";

static const char s_led_bpwm_set_schema[] =
    "{\"type\":\"object\",\"properties\":{\"duty\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":100}},\"required\":[\"duty\"],\"additionalProperties\":false}";

static const char s_led_auto_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"update\",\"stop\",\"status\"]},"
    "\"mode\":{\"type\":\"string\",\"enum\":[\"blink\"]},"
    "\"interval_ms\":{\"type\":\"integer\",\"minimum\":20,\"maximum\":5000},"
    "\"initial_on\":{\"type\":\"boolean\"},"
    "\"notify\":{\"type\":\"boolean\"}"
    "},\"required\":[\"action\"],\"additionalProperties\":false}";

typedef struct
{
    bool enabled;
    bool led_on;
    bool notify;
    uint32_t event_count;
    uint32_t interval_ms;
    TickType_t next_tick;
} nualink_led_auto_state_t;

static nualink_led_auto_state_t s_led_auto =
    {
        false,
        false,
        false,
        0U,
        NUALINK_LED_AUTO_DEFAULT_INTERVAL_MS,
        0U};

static void prvNotifyLedAutoEvent(void)
{
    char json_line[160];

    if (!s_led_auto.notify)
    {
        return;
    }

    s_led_auto.event_count++;
    (void)snprintf(json_line,
                   sizeof(json_line),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"led.auto.event\",\"params\":{\"pin\":\"PC14\",\"on\":%s,\"event_count\":%lu,\"tick\":%lu}}\n",
                   s_led_auto.led_on ? "true" : "false",
                   (unsigned long)s_led_auto.event_count,
                   (unsigned long)xTaskGetTickCount());
    (void)NuAILink_TasksPushNotification(json_line);
}

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

static int32_t prvBuildLedAutoResult(cJSON *result, const char *action)
{
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[112];

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
                   "PC14 LED auto %s: running=%lu mode=blink interval_ms=%lu on=%lu notify=%lu events=%lu",
                   action,
                   (unsigned long)(s_led_auto.enabled ? 1U : 0U),
                   (unsigned long)s_led_auto.interval_ms,
                   (unsigned long)(s_led_auto.led_on ? 1U : 0U),
                   (unsigned long)(s_led_auto.notify ? 1U : 0U),
                   (unsigned long)s_led_auto.event_count);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "pin", "PC14");
    (void)cJSON_AddStringToObject(structured, "action", action);
    (void)cJSON_AddStringToObject(structured, "mode", "blink");
    (void)cJSON_AddBoolToObject(structured, "running", s_led_auto.enabled ? 1 : 0);
    (void)cJSON_AddBoolToObject(structured, "on", s_led_auto.led_on ? 1 : 0);
    (void)cJSON_AddBoolToObject(structured, "notify", s_led_auto.notify ? 1 : 0);
    (void)cJSON_AddNumberToObject(structured, "event_count", (double)s_led_auto.event_count);
    (void)cJSON_AddNumberToObject(structured, "interval_ms", (double)s_led_auto.interval_ms);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);
    return MCP_STATUS_OK;
}

static int32_t prvLedSetCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *on;
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[48];
    bool led_on;

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    on = cJSON_GetObjectItemCaseSensitive(arguments, "on");
    if ((on == NULL) || !cJSON_IsBool(on))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    led_on = cJSON_IsTrue(on) ? true : false;
    NuAILink_BoardSetLed(led_on);

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

    (void)snprintf(message, sizeof(message), "PC14 LED is %s", led_on ? "on" : "off");
    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);
    (void)cJSON_AddBoolToObject(structured, "on", led_on ? 1 : 0);
    (void)cJSON_AddStringToObject(structured, "mode", "gpio");
    (void)cJSON_AddStringToObject(structured, "pin", "PC14");

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

static int32_t prvLedBpwmSetCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *duty;
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[64];
    uint32_t duty_percent;

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    duty = cJSON_GetObjectItemCaseSensitive(arguments, "duty");
    if ((duty == NULL) || !cJSON_IsNumber(duty))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    if ((duty->valueint < 0) || (duty->valueint > 100))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    duty_percent = (uint32_t)duty->valueint;
    if (!NuAILink_BoardLedBpwmSet(duty_percent))
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

    (void)snprintf(message, sizeof(message), "PC14 LED BPWM brightness %lu%%", (unsigned long)duty_percent);
    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "mode", "bpwm");
    (void)cJSON_AddStringToObject(structured, "pin", "PC14");
    (void)cJSON_AddNumberToObject(structured, "duty", (double)duty_percent);
    (void)cJSON_AddBoolToObject(structured, "activeLow", 1);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);

    return MCP_STATUS_OK;
}

static int32_t prvLedAutoCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *action_item;
    const cJSON *mode_item;
    const cJSON *initial_on_item;
    const cJSON *notify_item;
    const char *action;
    uint32_t interval_ms = s_led_auto.interval_ms;
    bool led_on = s_led_auto.led_on;
    bool notify = s_led_auto.notify;

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
        mode_item = cJSON_GetObjectItemCaseSensitive(arguments, "mode");
        if ((mode_item != NULL) && (!cJSON_IsString(mode_item) || (mode_item->valuestring == NULL) ||
                                    (strcmp(mode_item->valuestring, "blink") != 0)))
        {
            return MCP_STATUS_INVALID_PARAMS;
        }

        if (strcmp(action, "start") == 0)
        {
            interval_ms = NUALINK_LED_AUTO_DEFAULT_INTERVAL_MS;
            led_on = false;
            notify = false;
        }

        if (!prvGetU32InRange(arguments,
                              "interval_ms",
                              NUALINK_LED_AUTO_MIN_INTERVAL_MS,
                              NUALINK_LED_AUTO_MAX_INTERVAL_MS,
                              &interval_ms,
                              false))
        {
            return MCP_STATUS_INVALID_PARAMS;
        }

        initial_on_item = cJSON_GetObjectItemCaseSensitive(arguments, "initial_on");
        if (initial_on_item != NULL)
        {
            if (!cJSON_IsBool(initial_on_item))
            {
                return MCP_STATUS_INVALID_PARAMS;
            }
            led_on = cJSON_IsTrue(initial_on_item) ? true : false;
        }

        notify_item = cJSON_GetObjectItemCaseSensitive(arguments, "notify");
        if (notify_item != NULL)
        {
            if (!cJSON_IsBool(notify_item))
            {
                return MCP_STATUS_INVALID_PARAMS;
            }
            notify = cJSON_IsTrue(notify_item) ? true : false;
        }

        s_led_auto.interval_ms = interval_ms;
        s_led_auto.led_on = led_on;
        s_led_auto.notify = notify;

        if (strcmp(action, "start") == 0)
        {
            s_led_auto.enabled = true;
            s_led_auto.event_count = 0U;
        }

        if (s_led_auto.enabled)
        {
            NuAILink_BoardSetLed(s_led_auto.led_on);
            s_led_auto.next_tick = xTaskGetTickCount() + prvMsToTicksMin1(s_led_auto.interval_ms);
        }
    }
    else if (strcmp(action, "stop") == 0)
    {
        s_led_auto.enabled = false;
    }
    else if (strcmp(action, "status") != 0)
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    return prvBuildLedAutoResult(result, action);
}

void NuAILink_LedAutoProcess(void)
{
    TickType_t now;

    if (!s_led_auto.enabled)
    {
        return;
    }

    now = xTaskGetTickCount();
    if ((int32_t)(now - s_led_auto.next_tick) < 0)
    {
        return;
    }

    s_led_auto.led_on = s_led_auto.led_on ? false : true;
    NuAILink_BoardSetLed(s_led_auto.led_on);
    prvNotifyLedAutoEvent();
    s_led_auto.next_tick = now + prvMsToTicksMin1(s_led_auto.interval_ms);
}

const mcp_tool_t gNuAILinkLedSetTool =
    {
        "led.set",
        "Set the NuMaker PC14 active-low LED and stop the automatic heartbeat blink.",
        s_led_set_schema,
        prvLedSetCallback,
        NULL};

const mcp_tool_t gNuAILinkLedBpwmSetTool =
    {
        "led.bpwm.set",
        "Set PC14 LED brightness via BPWM2_CH0 duty (0..100). Calling led.set switches back to GPIO mode.",
        s_led_bpwm_set_schema,
        prvLedBpwmSetCallback,
        NULL};

const mcp_tool_t gNuAILinkLedAutoTool =
    {
        "led.auto",
        "Control autonomous PC14 LED blinking (start/update/stop/status).",
        s_led_auto_schema,
        prvLedAutoCallback,
        NULL};