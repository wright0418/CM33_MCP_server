/*
 * NuAILink Phase 2.1: GPIO read/write + PB14 button helpers.
 *
 * Tools provided:
 *   gpio.read   {port:"A".."H", pin:0..15}            -> {value:0|1}
 *   gpio.write  {port:"A".."H", pin:0..15, value:0|1} -> {ok:true}
 *   button.read {}                                    -> {pressed:bool}
 *   gpio.auto   {action,start/update args...}         -> autonomous GPIO toggle
 *
 * The button at PB14 is initialised in NuAILink_BoardButtonInit() and
 * pushes asynchronous "button.event" JSON-RPC notifications via the GPB ISR.
 */

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "FreeRTOS.h"
#include "nualink_board.h"
#include "nualink_tasks.h"
#include "task.h"

#define NUALINK_GPIO_AUTO_DEFAULT_PORT 'C'
#define NUALINK_GPIO_AUTO_DEFAULT_PIN 14U
#define NUALINK_GPIO_AUTO_DEFAULT_INTERVAL_MS 500U
#define NUALINK_GPIO_AUTO_MIN_INTERVAL_MS 20U
#define NUALINK_GPIO_AUTO_MAX_INTERVAL_MS 5000U

static const char s_gpio_read_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"port\":{\"type\":\"string\",\"pattern\":\"^[A-Ha-h]$\"},"
    "\"pin\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15}"
    "},\"required\":[\"port\",\"pin\"],\"additionalProperties\":false}";

static const char s_gpio_write_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"port\":{\"type\":\"string\",\"pattern\":\"^[A-Ha-h]$\"},"
    "\"pin\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15},"
    "\"value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1}"
    "},\"required\":[\"port\",\"pin\",\"value\"],\"additionalProperties\":false}";

static const char s_button_read_schema[] =
    "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

static const char s_gpio_auto_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"update\",\"stop\",\"status\"]},"
    "\"mode\":{\"type\":\"string\",\"enum\":[\"toggle\"]},"
    "\"port\":{\"type\":\"string\",\"pattern\":\"^[A-Ha-h]$\"},"
    "\"pin\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":15},"
    "\"initial_value\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":1},"
    "\"interval_ms\":{\"type\":\"integer\",\"minimum\":20,\"maximum\":5000},"
    "\"notify\":{\"type\":\"boolean\"}"
    "},\"required\":[\"action\"],\"additionalProperties\":false}";

typedef struct
{
    bool enabled;
    bool notify;
    char port;
    uint32_t pin;
    uint32_t value;
    uint32_t event_count;
    uint32_t interval_ms;
    TickType_t next_tick;
} nualink_gpio_auto_state_t;

static nualink_gpio_auto_state_t s_gpio_auto =
    {
        false,
        false,
        NUALINK_GPIO_AUTO_DEFAULT_PORT,
        NUALINK_GPIO_AUTO_DEFAULT_PIN,
        0U,
        0U,
        NUALINK_GPIO_AUTO_DEFAULT_INTERVAL_MS,
        0U};

static char prvNormalizePort(char port)
{
    return (char)((port >= 'a') ? (port - 'a' + 'A') : port);
}

static void prvNotifyGpioAutoEvent(void)
{
    char json_line[176];

    if (!s_gpio_auto.notify)
    {
        return;
    }

    s_gpio_auto.event_count++;
    (void)snprintf(json_line,
                   sizeof(json_line),
                   "{\"jsonrpc\":\"2.0\",\"method\":\"gpio.auto.event\",\"params\":{\"port\":\"%c\",\"pin\":%lu,\"value\":%lu,\"event_count\":%lu,\"tick\":%lu}}\n",
                   prvNormalizePort(s_gpio_auto.port),
                   (unsigned long)s_gpio_auto.pin,
                   (unsigned long)s_gpio_auto.value,
                   (unsigned long)s_gpio_auto.event_count,
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

static bool prvExtractPortPin(const cJSON *arguments, char *out_port, uint32_t *out_pin)
{
    const cJSON *port;
    const cJSON *pin;
    const char *port_str;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return false;
    }

    port = cJSON_GetObjectItemCaseSensitive(arguments, "port");
    pin = cJSON_GetObjectItemCaseSensitive(arguments, "pin");
    if ((port == NULL) || !cJSON_IsString(port) || (pin == NULL) || !cJSON_IsNumber(pin))
    {
        return false;
    }

    port_str = port->valuestring;
    if ((port_str == NULL) || (port_str[0] == '\0') || (port_str[1] != '\0'))
    {
        return false;
    }
    if ((pin->valueint < 0) || (pin->valueint > 15))
    {
        return false;
    }

    *out_port = port_str[0];
    *out_pin = (uint32_t)pin->valueint;
    return true;
}

static int32_t prvBuildContent(cJSON *result, const char *text, cJSON *structured)
{
    cJSON *content;
    cJSON *text_item;

    content = cJSON_CreateArray();
    text_item = cJSON_CreateObject();
    if ((content == NULL) || (text_item == NULL))
    {
        cJSON_Delete(content);
        cJSON_Delete(text_item);
        cJSON_Delete(structured);
        return MCP_STATUS_INTERNAL_ERROR;
    }

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", text);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);
    return MCP_STATUS_OK;
}

static int32_t prvGpioReadCallback(const cJSON *arguments, cJSON *result, void *context)
{
    char port;
    uint32_t pin;
    uint32_t value;
    cJSON *structured;
    char message[48];

    (void)context;

    if (!prvExtractPortPin(arguments, &port, &pin))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    if (!NuAILink_BoardGpioRead(port, pin, &value))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    structured = cJSON_CreateObject();
    if (structured == NULL)
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }
    (void)cJSON_AddStringToObject(structured, "port", (char[]){(char)((port >= 'a') ? (port - 'a' + 'A') : port), '\0'});
    (void)cJSON_AddNumberToObject(structured, "pin", (double)pin);
    (void)cJSON_AddNumberToObject(structured, "value", (double)value);

    (void)snprintf(message, sizeof(message), "P%c%lu = %lu",
                   (port >= 'a') ? (port - 'a' + 'A') : port,
                   (unsigned long)pin,
                   (unsigned long)value);

    return prvBuildContent(result, message, structured);
}

static int32_t prvGpioWriteCallback(const cJSON *arguments, cJSON *result, void *context)
{
    char port;
    uint32_t pin;
    const cJSON *value_item;
    uint32_t value;
    cJSON *structured;
    char message[48];

    (void)context;

    if (!prvExtractPortPin(arguments, &port, &pin))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    value_item = cJSON_GetObjectItemCaseSensitive(arguments, "value");
    if ((value_item == NULL) || !cJSON_IsNumber(value_item))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    if ((value_item->valueint != 0) && (value_item->valueint != 1))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    value = (uint32_t)value_item->valueint;

    if (!NuAILink_BoardGpioWrite(port, pin, value))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    structured = cJSON_CreateObject();
    if (structured == NULL)
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }
    (void)cJSON_AddStringToObject(structured, "port", (char[]){(char)((port >= 'a') ? (port - 'a' + 'A') : port), '\0'});
    (void)cJSON_AddNumberToObject(structured, "pin", (double)pin);
    (void)cJSON_AddNumberToObject(structured, "value", (double)value);
    (void)cJSON_AddBoolToObject(structured, "ok", 1);

    (void)snprintf(message, sizeof(message), "P%c%lu <- %lu",
                   (port >= 'a') ? (port - 'a' + 'A') : port,
                   (unsigned long)pin,
                   (unsigned long)value);

    return prvBuildContent(result, message, structured);
}

static int32_t prvButtonReadCallback(const cJSON *arguments, cJSON *result, void *context)
{
    cJSON *structured;
    bool pressed;

    (void)arguments;
    (void)context;

    pressed = NuAILink_BoardButtonIsPressed();

    structured = cJSON_CreateObject();
    if (structured == NULL)
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }
    (void)cJSON_AddStringToObject(structured, "pin", "PB14");
    (void)cJSON_AddBoolToObject(structured, "pressed", pressed ? 1 : 0);

    return prvBuildContent(result,
                           pressed ? "PB14 button is PRESSED" : "PB14 button is released",
                           structured);
}

static int32_t prvBuildGpioAutoResult(cJSON *result, const char *action)
{
    cJSON *structured;
    char message[96];

    structured = cJSON_CreateObject();
    if (structured == NULL)
    {
        return MCP_STATUS_INTERNAL_ERROR;
    }

    (void)cJSON_AddStringToObject(structured, "action", action);
    (void)cJSON_AddStringToObject(structured, "mode", "toggle");
    (void)cJSON_AddBoolToObject(structured, "running", s_gpio_auto.enabled ? 1 : 0);
    (void)cJSON_AddStringToObject(structured, "port", (char[]){prvNormalizePort(s_gpio_auto.port), '\0'});
    (void)cJSON_AddNumberToObject(structured, "pin", (double)s_gpio_auto.pin);
    (void)cJSON_AddNumberToObject(structured, "value", (double)s_gpio_auto.value);
    (void)cJSON_AddBoolToObject(structured, "notify", s_gpio_auto.notify ? 1 : 0);
    (void)cJSON_AddNumberToObject(structured, "event_count", (double)s_gpio_auto.event_count);
    (void)cJSON_AddNumberToObject(structured, "interval_ms", (double)s_gpio_auto.interval_ms);

    (void)snprintf(message,
                   sizeof(message),
                   "GPIO auto %s: running=%lu P%c%lu toggle interval_ms=%lu value=%lu notify=%lu events=%lu",
                   action,
                   (unsigned long)(s_gpio_auto.enabled ? 1U : 0U),
                   prvNormalizePort(s_gpio_auto.port),
                   (unsigned long)s_gpio_auto.pin,
                   (unsigned long)s_gpio_auto.interval_ms,
                   (unsigned long)s_gpio_auto.value,
                   (unsigned long)(s_gpio_auto.notify ? 1U : 0U),
                   (unsigned long)s_gpio_auto.event_count);

    return prvBuildContent(result, message, structured);
}

static int32_t prvGpioAutoCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *action_item;
    const cJSON *mode_item;
    const cJSON *port_item;
    const cJSON *notify_item;
    const char *action;
    const char *port_str;
    char port = s_gpio_auto.port;
    uint32_t pin = s_gpio_auto.pin;
    uint32_t value = s_gpio_auto.value;
    uint32_t interval_ms = s_gpio_auto.interval_ms;
    bool notify = s_gpio_auto.notify;

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
            port = NUALINK_GPIO_AUTO_DEFAULT_PORT;
            pin = NUALINK_GPIO_AUTO_DEFAULT_PIN;
            value = 0U;
            interval_ms = NUALINK_GPIO_AUTO_DEFAULT_INTERVAL_MS;
            notify = false;
        }

        mode_item = cJSON_GetObjectItemCaseSensitive(arguments, "mode");
        if ((mode_item != NULL) && (!cJSON_IsString(mode_item) || (mode_item->valuestring == NULL) ||
                                    (strcmp(mode_item->valuestring, "toggle") != 0)))
        {
            return MCP_STATUS_INVALID_PARAMS;
        }

        port_item = cJSON_GetObjectItemCaseSensitive(arguments, "port");
        if (port_item != NULL)
        {
            if (!cJSON_IsString(port_item) || (port_item->valuestring == NULL))
            {
                return MCP_STATUS_INVALID_PARAMS;
            }
            port_str = port_item->valuestring;
            if ((port_str[0] == '\0') || (port_str[1] != '\0') ||
                !(((port_str[0] >= 'A') && (port_str[0] <= 'H')) || ((port_str[0] >= 'a') && (port_str[0] <= 'h'))))
            {
                return MCP_STATUS_INVALID_PARAMS;
            }
            port = port_str[0];
        }

        if (!prvGetU32InRange(arguments, "pin", 0U, 15U, &pin, false) ||
            !prvGetU32InRange(arguments, "initial_value", 0U, 1U, &value, false) ||
            !prvGetU32InRange(arguments,
                              "interval_ms",
                              NUALINK_GPIO_AUTO_MIN_INTERVAL_MS,
                              NUALINK_GPIO_AUTO_MAX_INTERVAL_MS,
                              &interval_ms,
                              false))
        {
            return MCP_STATUS_INVALID_PARAMS;
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

        s_gpio_auto.port = port;
        s_gpio_auto.pin = pin;
        s_gpio_auto.value = value;
        s_gpio_auto.interval_ms = interval_ms;
        s_gpio_auto.notify = notify;

        if (strcmp(action, "start") == 0)
        {
            s_gpio_auto.enabled = true;
            s_gpio_auto.event_count = 0U;
        }

        if (s_gpio_auto.enabled)
        {
            if (!NuAILink_BoardGpioWrite(s_gpio_auto.port, s_gpio_auto.pin, s_gpio_auto.value))
            {
                s_gpio_auto.enabled = false;
                return MCP_STATUS_INVALID_PARAMS;
            }
            s_gpio_auto.next_tick = xTaskGetTickCount() + prvMsToTicksMin1(s_gpio_auto.interval_ms);
        }
    }
    else if (strcmp(action, "stop") == 0)
    {
        s_gpio_auto.enabled = false;
    }
    else if (strcmp(action, "status") != 0)
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    return prvBuildGpioAutoResult(result, action);
}

void NuAILink_GpioAutoProcess(void)
{
    TickType_t now;

    if (!s_gpio_auto.enabled)
    {
        return;
    }

    now = xTaskGetTickCount();
    if ((int32_t)(now - s_gpio_auto.next_tick) < 0)
    {
        return;
    }

    s_gpio_auto.value = (s_gpio_auto.value == 0U) ? 1U : 0U;
    if (!NuAILink_BoardGpioWrite(s_gpio_auto.port, s_gpio_auto.pin, s_gpio_auto.value))
    {
        s_gpio_auto.enabled = false;
        return;
    }
    prvNotifyGpioAutoEvent();
    s_gpio_auto.next_tick = now + prvMsToTicksMin1(s_gpio_auto.interval_ms);
}

const mcp_tool_t gNuAILinkGpioReadTool =
    {
        "gpio.read",
        "Read a GPIO pin level (port A..H, pin 0..15).",
        s_gpio_read_schema,
        prvGpioReadCallback,
        NULL};

const mcp_tool_t gNuAILinkGpioWriteTool =
    {
        "gpio.write",
        "Set a GPIO pin output (forces OUTPUT mode). PB12/PB13/PB14 are reserved.",
        s_gpio_write_schema,
        prvGpioWriteCallback,
        NULL};

const mcp_tool_t gNuAILinkButtonReadTool =
    {
        "button.read",
        "Read the current PB14 button state (active-low, pull-up).",
        s_button_read_schema,
        prvButtonReadCallback,
        NULL};

const mcp_tool_t gNuAILinkGpioAutoTool =
    {
        "gpio.auto",
        "Control autonomous GPIO output toggle (start/update/stop/status).",
        s_gpio_auto_schema,
        prvGpioAutoCallback,
        NULL};
