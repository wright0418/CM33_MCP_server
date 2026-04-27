/*
 * NuAILink Phase 2.4+: LLSI (WS2812) helpers.
 *
 * Tools provided:
 *   llsi.fill    {r,g,b,count?}                       -> fill first N pixels (default: 10)
 *   llsi.pattern {pattern,count?,phase?,r?,g?,b?}    -> pattern frame for animation
 */

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "nualink_board.h"

#define NUALINK_LLSI_DEFAULT_COUNT 10U
#define NUALINK_LLSI_MAX_COUNT 10U
#define NUALINK_LLSI_DEFAULT_PHASE 0U
#define NUALINK_LLSI_DEFAULT_RED 255U
#define NUALINK_LLSI_DEFAULT_GREEN 0U
#define NUALINK_LLSI_DEFAULT_BLUE 0U
#define NUALINK_LLSI_MAX_PHASE 4095U

static const char s_llsi_fill_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"r\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"g\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"b\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10}"
    "},\"required\":[\"r\",\"g\",\"b\"],\"additionalProperties\":false}";

static const char s_llsi_pattern_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"pattern\":{\"type\":\"string\",\"enum\":[\"off\",\"solid\",\"chase\",\"gradient\",\"rainbow\"]},"
    "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10},"
    "\"phase\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":4095},"
    "\"r\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"g\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"b\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}"
    "},\"required\":[\"pattern\"],\"additionalProperties\":false}";

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

static bool prvIsSupportedPattern(const char *pattern)
{
    if (pattern == NULL)
    {
        return false;
    }

    return (strcmp(pattern, "off") == 0) ||
           (strcmp(pattern, "solid") == 0) ||
           (strcmp(pattern, "chase") == 0) ||
           (strcmp(pattern, "gradient") == 0) ||
           (strcmp(pattern, "rainbow") == 0);
}

static void prvWheelToRgb(uint8_t wheel, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b)
{
    if ((out_r == NULL) || (out_g == NULL) || (out_b == NULL))
    {
        return;
    }

    if (wheel < 85U)
    {
        *out_r = (uint8_t)(255U - (wheel * 3U));
        *out_g = (uint8_t)(wheel * 3U);
        *out_b = 0U;
    }
    else if (wheel < 170U)
    {
        wheel = (uint8_t)(wheel - 85U);
        *out_r = 0U;
        *out_g = (uint8_t)(255U - (wheel * 3U));
        *out_b = (uint8_t)(wheel * 3U);
    }
    else
    {
        wheel = (uint8_t)(wheel - 170U);
        *out_r = (uint8_t)(wheel * 3U);
        *out_g = 0U;
        *out_b = (uint8_t)(255U - (wheel * 3U));
    }
}

static void prvBuildPatternFrame(const char *pattern,
                                 uint8_t *frame,
                                 uint32_t count,
                                 uint32_t phase,
                                 uint32_t red,
                                 uint32_t green,
                                 uint32_t blue)
{
    uint32_t i;

    for (i = 0U; i < count; i++)
    {
        frame[i * 3U + 0U] = 0U;
        frame[i * 3U + 1U] = 0U;
        frame[i * 3U + 2U] = 0U;
    }

    if (strcmp(pattern, "off") == 0)
    {
        return;
    }

    if (strcmp(pattern, "solid") == 0)
    {
        for (i = 0U; i < count; i++)
        {
            frame[i * 3U + 0U] = (uint8_t)red;
            frame[i * 3U + 1U] = (uint8_t)green;
            frame[i * 3U + 2U] = (uint8_t)blue;
        }
        return;
    }

    if (strcmp(pattern, "chase") == 0)
    {
        uint32_t on_index = phase % count;
        frame[on_index * 3U + 0U] = (uint8_t)red;
        frame[on_index * 3U + 1U] = (uint8_t)green;
        frame[on_index * 3U + 2U] = (uint8_t)blue;
        return;
    }

    if (strcmp(pattern, "gradient") == 0)
    {
        for (i = 0U; i < count; i++)
        {
            uint32_t idx = (i + phase) % count;
            uint32_t level = (count <= 1U) ? 255U : ((idx * 255U) / (count - 1U));
            frame[i * 3U + 0U] = (uint8_t)(((red * level) + 127U) / 255U);
            frame[i * 3U + 1U] = (uint8_t)(((green * level) + 127U) / 255U);
            frame[i * 3U + 2U] = (uint8_t)(((blue * level) + 127U) / 255U);
        }
        return;
    }

    /* rainbow */
    for (i = 0U; i < count; i++)
    {
        uint8_t r8;
        uint8_t g8;
        uint8_t b8;
        uint8_t wheel = (uint8_t)(((phase * 8U) + ((i * 256U) / count)) & 0xFFU);

        prvWheelToRgb(wheel, &r8, &g8, &b8);
        frame[i * 3U + 0U] = r8;
        frame[i * 3U + 1U] = g8;
        frame[i * 3U + 2U] = b8;
    }
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

static int32_t prvLlsiPatternCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *pattern_item;
    const char *pattern;
    uint32_t red = NUALINK_LLSI_DEFAULT_RED;
    uint32_t green = NUALINK_LLSI_DEFAULT_GREEN;
    uint32_t blue = NUALINK_LLSI_DEFAULT_BLUE;
    uint32_t count = NUALINK_LLSI_DEFAULT_COUNT;
    uint32_t phase = NUALINK_LLSI_DEFAULT_PHASE;
    uint8_t frame[NUALINK_LLSI_MAX_COUNT * 3U];
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[128];

    (void)context;

    if ((arguments == NULL) || !cJSON_IsObject(arguments))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    pattern_item = cJSON_GetObjectItemCaseSensitive(arguments, "pattern");
    if ((pattern_item == NULL) || !cJSON_IsString(pattern_item) || (pattern_item->valuestring == NULL))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }
    pattern = pattern_item->valuestring;
    if (!prvIsSupportedPattern(pattern))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    if (!prvGetU32InRange(arguments, "count", 1U, NUALINK_LLSI_MAX_COUNT, &count, false) ||
        !prvGetU32InRange(arguments, "phase", 0U, NUALINK_LLSI_MAX_PHASE, &phase, false) ||
        !prvGetU32InRange(arguments, "r", 0U, 255U, &red, false) ||
        !prvGetU32InRange(arguments, "g", 0U, 255U, &green, false) ||
        !prvGetU32InRange(arguments, "b", 0U, 255U, &blue, false))
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    prvBuildPatternFrame(pattern, frame, count, phase, red, green, blue);
    if (!NuAILink_BoardLlsiWritePixels(frame, count))
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
                   "LLSI pattern PB15: %s count=%lu phase=%lu rgb=(%lu,%lu,%lu)",
                   pattern,
                   (unsigned long)count,
                   (unsigned long)phase,
                   (unsigned long)red,
                   (unsigned long)green,
                   (unsigned long)blue);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "pin", "PB15");
    (void)cJSON_AddStringToObject(structured, "pattern", pattern);
    (void)cJSON_AddNumberToObject(structured, "count", (double)count);
    (void)cJSON_AddNumberToObject(structured, "phase", (double)phase);
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

const mcp_tool_t gNuAILinkLlsiPatternTool =
    {
        "llsi.pattern",
        "Render one pattern frame on LLSI0(PB15) for animation stepping.",
        s_llsi_pattern_schema,
        prvLlsiPatternCallback,
        NULL};
