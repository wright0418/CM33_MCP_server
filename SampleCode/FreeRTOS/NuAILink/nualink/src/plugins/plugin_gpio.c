/*
 * NuAILink Phase 2.1: GPIO read/write + PB14 button helpers.
 *
 * Tools provided:
 *   gpio.read   {port:"A".."H", pin:0..15}            -> {value:0|1}
 *   gpio.write  {port:"A".."H", pin:0..15, value:0|1} -> {ok:true}
 *   button.read {}                                    -> {pressed:bool}
 *
 * The button at PB14 is initialised in NuAILink_BoardButtonInit() and
 * pushes asynchronous "button.event" JSON-RPC notifications via the GPB ISR.
 */

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "nualink_board.h"

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
