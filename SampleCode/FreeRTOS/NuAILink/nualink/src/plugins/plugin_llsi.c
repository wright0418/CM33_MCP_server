/*
 * NuAILink Phase 2.4+: LLSI (WS2812) helpers.
 *
 * Tools provided:
 *   llsi.fill    {r,g,b,count?}                       -> fill first N pixels (default: 10)
 *   llsi.pattern {pattern,count?,phase?,r?,g?,b?}    -> pattern frame for animation
 *   llsi.autoplay {action,start/update args...}       -> autonomous playback control
 */

#include "mcp_types.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "FreeRTOS.h"
#include "nualink_board.h"
#include "task.h"

#define NUALINK_LLSI_DEFAULT_COUNT 10U
#define NUALINK_LLSI_MAX_COUNT 10U
#define NUALINK_LLSI_DEFAULT_PHASE 0U
#define NUALINK_LLSI_DEFAULT_STEP 1U
#define NUALINK_LLSI_DEFAULT_INTERVAL_MS 50U
#define NUALINK_LLSI_DEFAULT_RED 255U
#define NUALINK_LLSI_DEFAULT_GREEN 0U
#define NUALINK_LLSI_DEFAULT_BLUE 0U
#define NUALINK_LLSI_DEFAULT_PATTERN "rainbow"
#define NUALINK_LLSI_MAX_PHASE 4095U
#define NUALINK_LLSI_MAX_STEP NUALINK_LLSI_MAX_PHASE
#define NUALINK_LLSI_MIN_INTERVAL_MS 10U
#define NUALINK_LLSI_MAX_INTERVAL_MS 5000U

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

static const char s_llsi_autoplay_schema[] =
    "{\"type\":\"object\",\"properties\":{"
    "\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"update\",\"stop\",\"status\"]},"
    "\"pattern\":{\"type\":\"string\",\"enum\":[\"off\",\"solid\",\"chase\",\"gradient\",\"rainbow\"]},"
    "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10},"
    "\"phase\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":4095},"
    "\"step\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":4095},"
    "\"interval_ms\":{\"type\":\"integer\",\"minimum\":10,\"maximum\":5000},"
    "\"r\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"g\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255},"
    "\"b\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":255}"
    "},\"required\":[\"action\"],\"additionalProperties\":false}";

typedef struct
{
    bool enabled;
    char pattern[16];
    uint32_t count;
    uint32_t phase;
    uint32_t step;
    uint32_t interval_ms;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    TickType_t next_tick;
} nualink_llsi_autoplay_state_t;

static nualink_llsi_autoplay_state_t s_llsi_autoplay =
    {
        false,
        NUALINK_LLSI_DEFAULT_PATTERN,
        NUALINK_LLSI_DEFAULT_COUNT,
        NUALINK_LLSI_DEFAULT_PHASE,
        NUALINK_LLSI_DEFAULT_STEP,
        NUALINK_LLSI_DEFAULT_INTERVAL_MS,
        NUALINK_LLSI_DEFAULT_RED,
        NUALINK_LLSI_DEFAULT_GREEN,
        NUALINK_LLSI_DEFAULT_BLUE,
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

static bool prvRenderAutoplayFrameAndAdvance(void)
{
    uint8_t frame[NUALINK_LLSI_MAX_COUNT * 3U];

    prvBuildPatternFrame(s_llsi_autoplay.pattern,
                         frame,
                         s_llsi_autoplay.count,
                         s_llsi_autoplay.phase,
                         s_llsi_autoplay.red,
                         s_llsi_autoplay.green,
                         s_llsi_autoplay.blue);

    if (!NuAILink_BoardLlsiWritePixels(frame, s_llsi_autoplay.count))
    {
        return false;
    }

    s_llsi_autoplay.phase = (s_llsi_autoplay.phase + s_llsi_autoplay.step) & NUALINK_LLSI_MAX_PHASE;
    return true;
}

static int32_t prvBuildAutoplayResult(cJSON *result, const char *action)
{
    cJSON *content;
    cJSON *text_item;
    cJSON *structured;
    char message[160];

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
                   "LLSI autoplay %s: running=%lu pattern=%s count=%lu phase=%lu step=%lu interval_ms=%lu rgb=(%lu,%lu,%lu)",
                   action,
                   (unsigned long)(s_llsi_autoplay.enabled ? 1U : 0U),
                   s_llsi_autoplay.pattern,
                   (unsigned long)s_llsi_autoplay.count,
                   (unsigned long)s_llsi_autoplay.phase,
                   (unsigned long)s_llsi_autoplay.step,
                   (unsigned long)s_llsi_autoplay.interval_ms,
                   (unsigned long)s_llsi_autoplay.red,
                   (unsigned long)s_llsi_autoplay.green,
                   (unsigned long)s_llsi_autoplay.blue);

    (void)cJSON_AddStringToObject(text_item, "type", "text");
    (void)cJSON_AddStringToObject(text_item, "text", message);
    (void)cJSON_AddItemToArray(content, text_item);

    (void)cJSON_AddStringToObject(structured, "pin", "PB15");
    (void)cJSON_AddStringToObject(structured, "action", action);
    (void)cJSON_AddBoolToObject(structured, "running", s_llsi_autoplay.enabled ? 1 : 0);
    (void)cJSON_AddStringToObject(structured, "pattern", s_llsi_autoplay.pattern);
    (void)cJSON_AddNumberToObject(structured, "count", (double)s_llsi_autoplay.count);
    (void)cJSON_AddNumberToObject(structured, "phase", (double)s_llsi_autoplay.phase);
    (void)cJSON_AddNumberToObject(structured, "step", (double)s_llsi_autoplay.step);
    (void)cJSON_AddNumberToObject(structured, "interval_ms", (double)s_llsi_autoplay.interval_ms);
    (void)cJSON_AddNumberToObject(structured, "r", (double)s_llsi_autoplay.red);
    (void)cJSON_AddNumberToObject(structured, "g", (double)s_llsi_autoplay.green);
    (void)cJSON_AddNumberToObject(structured, "b", (double)s_llsi_autoplay.blue);

    (void)cJSON_AddItemToObject(result, "content", content);
    (void)cJSON_AddItemToObject(result, "structuredContent", structured);
    (void)cJSON_AddBoolToObject(result, "isError", 0);
    return MCP_STATUS_OK;
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

static int32_t prvLlsiAutoplayCallback(const cJSON *arguments, cJSON *result, void *context)
{
    const cJSON *action_item;
    const cJSON *pattern_item;
    const char *action;
    const char *pattern = NUALINK_LLSI_DEFAULT_PATTERN;
    uint32_t red = NUALINK_LLSI_DEFAULT_RED;
    uint32_t green = NUALINK_LLSI_DEFAULT_GREEN;
    uint32_t blue = NUALINK_LLSI_DEFAULT_BLUE;
    uint32_t count = NUALINK_LLSI_DEFAULT_COUNT;
    uint32_t phase = NUALINK_LLSI_DEFAULT_PHASE;
    uint32_t step = NUALINK_LLSI_DEFAULT_STEP;
    uint32_t interval_ms = NUALINK_LLSI_DEFAULT_INTERVAL_MS;

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
        if (strcmp(action, "update") == 0)
        {
            pattern = s_llsi_autoplay.pattern;
            count = s_llsi_autoplay.count;
            phase = s_llsi_autoplay.phase;
            step = s_llsi_autoplay.step;
            interval_ms = s_llsi_autoplay.interval_ms;
            red = s_llsi_autoplay.red;
            green = s_llsi_autoplay.green;
            blue = s_llsi_autoplay.blue;
        }

        pattern_item = cJSON_GetObjectItemCaseSensitive(arguments, "pattern");
        if (pattern_item != NULL)
        {
            if (!cJSON_IsString(pattern_item) || (pattern_item->valuestring == NULL))
            {
                return MCP_STATUS_INVALID_PARAMS;
            }
            pattern = pattern_item->valuestring;
        }

        if (!prvIsSupportedPattern(pattern) ||
            !prvGetU32InRange(arguments, "count", 1U, NUALINK_LLSI_MAX_COUNT, &count, false) ||
            !prvGetU32InRange(arguments, "phase", 0U, NUALINK_LLSI_MAX_PHASE, &phase, false) ||
            !prvGetU32InRange(arguments, "step", 1U, NUALINK_LLSI_MAX_STEP, &step, false) ||
            !prvGetU32InRange(arguments,
                              "interval_ms",
                              NUALINK_LLSI_MIN_INTERVAL_MS,
                              NUALINK_LLSI_MAX_INTERVAL_MS,
                              &interval_ms,
                              false) ||
            !prvGetU32InRange(arguments, "r", 0U, 255U, &red, false) ||
            !prvGetU32InRange(arguments, "g", 0U, 255U, &green, false) ||
            !prvGetU32InRange(arguments, "b", 0U, 255U, &blue, false))
        {
            return MCP_STATUS_INVALID_PARAMS;
        }

        (void)strncpy(s_llsi_autoplay.pattern, pattern, sizeof(s_llsi_autoplay.pattern) - 1U);
        s_llsi_autoplay.pattern[sizeof(s_llsi_autoplay.pattern) - 1U] = '\0';
        s_llsi_autoplay.count = count;
        s_llsi_autoplay.phase = phase;
        s_llsi_autoplay.step = step;
        s_llsi_autoplay.interval_ms = interval_ms;
        s_llsi_autoplay.red = red;
        s_llsi_autoplay.green = green;
        s_llsi_autoplay.blue = blue;

        if (strcmp(action, "start") == 0)
        {
            s_llsi_autoplay.enabled = true;
        }

        if (s_llsi_autoplay.enabled && !prvRenderAutoplayFrameAndAdvance())
        {
            s_llsi_autoplay.enabled = false;
            return MCP_STATUS_INTERNAL_ERROR;
        }

        if (s_llsi_autoplay.enabled)
        {
            s_llsi_autoplay.next_tick = xTaskGetTickCount() + prvMsToTicksMin1(s_llsi_autoplay.interval_ms);
        }
    }
    else if (strcmp(action, "stop") == 0)
    {
        s_llsi_autoplay.enabled = false;
    }
    else if (strcmp(action, "status") != 0)
    {
        return MCP_STATUS_INVALID_PARAMS;
    }

    return prvBuildAutoplayResult(result, action);
}

void NuAILink_LlsiAutoplayProcess(void)
{
    TickType_t now;

    if (!s_llsi_autoplay.enabled)
    {
        return;
    }

    now = xTaskGetTickCount();
    if ((int32_t)(now - s_llsi_autoplay.next_tick) < 0)
    {
        return;
    }

    if (!prvRenderAutoplayFrameAndAdvance())
    {
        s_llsi_autoplay.enabled = false;
        return;
    }

    s_llsi_autoplay.next_tick = now + prvMsToTicksMin1(s_llsi_autoplay.interval_ms);
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

const mcp_tool_t gNuAILinkLlsiAutoplayTool =
    {
        "llsi.autoplay",
        "Control autonomous LLSI0(PB15) pattern playback (start/update/stop/status).",
        s_llsi_autoplay_schema,
        prvLlsiAutoplayCallback,
        NULL};
