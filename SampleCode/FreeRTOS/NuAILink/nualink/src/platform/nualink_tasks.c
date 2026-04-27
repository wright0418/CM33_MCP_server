#include "nualink_tasks.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "NuMicro.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "cJSON.h"
#include "mcp_jsonrpc.h"
#include "nualink_board.h"
#include "nualink_config.h"
#include "nualink_log.h"
#include "nualink_transport.h"

/* Implemented by peripheral plugins: advance MCU-side autonomous modes when due. */
void NuAILink_LedAutoProcess(void);
void NuAILink_GpioAutoProcess(void);
void NuAILink_EadcAutoProcess(void);
void NuAILink_LlsiAutoplayProcess(void);

static void prvProcessPeripheralAutoModes(void)
{
    NuAILink_LedAutoProcess();
    NuAILink_GpioAutoProcess();
    NuAILink_EadcAutoProcess();
    NuAILink_LlsiAutoplayProcess();
}

typedef struct
{
    uint32_t length;
    char data[NUALINK_JSON_REQUEST_SIZE];
} nualink_request_t;

typedef struct
{
    uint32_t length;
    char data[NUALINK_JSON_RESPONSE_SIZE];
} nualink_response_t;

static QueueHandle_t s_request_queue;
static QueueHandle_t s_response_queue;
static StaticQueue_t s_request_queue_control;
static StaticQueue_t s_response_queue_control;

#if defined(__ICCARM__)
#pragma data_alignment = 4
static uint8_t s_request_queue_storage[NUALINK_REQUEST_QUEUE_LENGTH * sizeof(nualink_request_t)];
#pragma data_alignment = 4
static uint8_t s_response_queue_storage[NUALINK_RESPONSE_QUEUE_LENGTH * sizeof(nualink_response_t)];
#else
static uint8_t s_request_queue_storage[NUALINK_REQUEST_QUEUE_LENGTH * sizeof(nualink_request_t)] __attribute__((aligned(4)));
static uint8_t s_response_queue_storage[NUALINK_RESPONSE_QUEUE_LENGTH * sizeof(nualink_response_t)] __attribute__((aligned(4)));
#endif

static StaticTask_t s_usb_task_tcb;
static StaticTask_t s_parser_task_tcb;
static StaticTask_t s_heartbeat_task_tcb;
#if defined(__ICCARM__)
#pragma data_alignment = 8
static StackType_t s_usb_task_stack[2048];
#pragma data_alignment = 8
static StackType_t s_parser_task_stack[4096];
#pragma data_alignment = 8
static StackType_t s_heartbeat_task_stack[1024];
#else
static StackType_t s_usb_task_stack[2048] __attribute__((aligned(8)));
static StackType_t s_parser_task_stack[4096] __attribute__((aligned(8)));
static StackType_t s_heartbeat_task_stack[1024] __attribute__((aligned(8)));
#endif

static void *prvCJSONMalloc(size_t size)
{
    return pvPortMalloc(size);
}

static void prvCJSONFree(void *pointer)
{
    vPortFree(pointer);
}

void NuAILink_CJSONInitHooks(void)
{
    cJSON_Hooks hooks;

    hooks.malloc_fn = prvCJSONMalloc;
    hooks.free_fn = prvCJSONFree;
    cJSON_InitHooks(&hooks);
}

static void prvQueueImmediateResponse(const char *text)
{
    nualink_response_t response;
    size_t text_length;

    text_length = strlen(text);
    if (text_length >= sizeof(response.data))
    {
        text_length = sizeof(response.data) - 1U;
    }

    memcpy(response.data, text, text_length);
    response.data[text_length] = '\0';
    response.length = (uint32_t)text_length;
    (void)xQueueSend(s_response_queue, &response, 0U);
}

static BaseType_t prvHandleRequestInline(const nualink_request_t *request)
{
    nualink_response_t response;
    int32_t length;

    if ((request == NULL) || (request->length == 0U))
    {
        return pdFAIL;
    }

    response.length = 0U;
    response.data[0] = '\0';

    length = MCP_JSONRPC_Handle(request->data, response.data, sizeof(response.data));
    if (length <= 0)
    {
        return pdPASS;
    }

    response.length = (uint32_t)length;
    return xQueueSend(s_response_queue, &response, pdMS_TO_TICKS(20U));
}

static void prvUsbCommTask(void *parameters)
{
    static uint8_t packet[NUALINK_USB_RX_PACKET_SIZE];
    static nualink_request_t request;
    static nualink_response_t response;
    bool discarding_oversized_message = false;
    bool last_attached = false;
    uint8_t last_configured = 0xffU;
    uint8_t last_addr = 0xffU;
    uint32_t last_hispeed_en = 0xffffffffU;
    TickType_t last_diag_tick = 0U;

    (void)parameters;
    memset(&request, 0, sizeof(request));
    NUALINK_LOG("[USB] comm task start, calling NuAILink_TransportInit()\n");
    NuAILink_TransportInit();
    NUALINK_LOG("[USB] transport init done\n");

    for (;;)
    {
        uint32_t packet_length;
        bool attached;
        uint8_t configured;
        uint8_t addr;
        uint32_t hispeed_en;
        uint32_t gint_pending;
        uint32_t busint_pending;
        uint32_t busint_raw;
        TickType_t now;

        NuAILink_TransportPoll();

        attached = NuAILink_TransportIsAttached();
        configured = g_hsusbd_Configured;
        addr = g_hsusbd_UsbAddr;
        hispeed_en = (HSUSBD->OPER & HSUSBD_OPER_HISPDEN_Msk) ? 1U : 0U;
        gint_pending = HSUSBD->GINTSTS & HSUSBD->GINTEN;
        busint_pending = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;
        busint_raw = HSUSBD->BUSINTSTS;
        now = xTaskGetTickCount();

        if ((attached != last_attached) ||
            (configured != last_configured) ||
            (addr != last_addr) ||
            (hispeed_en != last_hispeed_en) ||
            ((now - last_diag_tick) >= pdMS_TO_TICKS(1000U)))
        {
            NUALINK_LOG("[USB] attached=%u cfg=%u addr=%u hs=%lu PHYCTL=0x%08lX OPER=0x%08lX GINTPEND=0x%08lX BUSPEND=0x%08lX BUSRAW=0x%08lX\n",
                        (unsigned int)(attached ? 1U : 0U),
                        (unsigned int)configured,
                        (unsigned int)addr,
                        (unsigned long)hispeed_en,
                        (unsigned long)HSUSBD->PHYCTL,
                        (unsigned long)HSUSBD->OPER,
                        (unsigned long)gint_pending,
                        (unsigned long)busint_pending,
                        (unsigned long)busint_raw);

            last_attached = attached;
            last_configured = configured;
            last_addr = addr;
            last_hispeed_en = hispeed_en;
            last_diag_tick = now;
        }

        while (xQueueReceive(s_response_queue, &response, 0U) == pdPASS)
        {
            (void)NuAILink_TransportWrite((const uint8_t *)response.data,
                                          response.length,
                                          pdMS_TO_TICKS(NUALINK_USB_TX_TIMEOUT_MS));
            NUALINK_LOG("[USB] tx queued response bytes=%lu\n", (unsigned long)response.length);
        }

        packet_length = NuAILink_TransportReadPacket(packet, sizeof(packet));
        if (packet_length > 0U)
        {
            uint32_t index;

            NUALINK_LOG("[USB] rx packet bytes=%lu\n", (unsigned long)packet_length);

            for (index = 0U; index < packet_length; index++)
            {
                uint8_t byte_value = packet[index];

                if (byte_value == '\r')
                {
                    continue;
                }

                if (byte_value == '\n')
                {
                    if (discarding_oversized_message)
                    {
                        discarding_oversized_message = false;
                        request.length = 0U;
                        prvQueueImmediateResponse("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"message_too_large\"},\"id\":null}\n");
                    }
                    else if (request.length > 0U)
                    {
                        request.data[request.length] = '\0';
                        NUALINK_LOG("[USB] frame ready len=%lu text=%.120s\n",
                                    (unsigned long)request.length,
                                    request.data);
                        if (prvHandleRequestInline(&request) != pdPASS)
                        {
                            NUALINK_ERR("[USB] inline handle failed -> server_busy\n");
                            prvQueueImmediateResponse("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"server_busy\"},\"id\":null}\n");
                        }
                        request.length = 0U;
                    }
                    else
                    {
                        request.length = 0U;
                    }
                }
                else if (!discarding_oversized_message)
                {
                    if (request.length < (sizeof(request.data) - 1U))
                    {
                        request.data[request.length] = (char)byte_value;
                        request.length++;
                    }
                    else
                    {
                        discarding_oversized_message = true;
                        request.length = 0U;
                    }
                }
                else
                {
                    /* Keep discarding bytes until the next newline frame boundary. */
                }
            }
        }

        prvProcessPeripheralAutoModes();

        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

static void prvParserTask(void *parameters)
{
    nualink_request_t request;
    nualink_response_t response;

    (void)parameters;
    NUALINK_LOG("[MCP] parser task start\n");

    for (;;)
    {
        if (xQueueReceive(s_request_queue, &request, portMAX_DELAY) == pdPASS)
        {
            int32_t length;

            response.length = 0U;
            response.data[0] = '\0';

            NUALINK_LOG("[MCP] handle request: %.120s\n", request.data);

            length = MCP_JSONRPC_Handle(request.data, response.data, sizeof(response.data));
            if (length > 0)
            {
                response.length = (uint32_t)length;
                NUALINK_LOG("[MCP] response bytes=%ld text=%.120s\n", (long)length, response.data);
                (void)xQueueSend(s_response_queue, &response, pdMS_TO_TICKS(100U));
            }
            else
            {
                NUALINK_LOG("[MCP] no response generated (len=%ld)\n", (long)length);
            }
        }
    }
}

static void prvHeartbeatTask(void *parameters)
{
    (void)parameters;
    NUALINK_LOG("[SYS] heartbeat task start\n");

    for (;;)
    {
        NuAILink_BoardHeartbeatToggle();
        vTaskDelay(pdMS_TO_TICKS(NUALINK_HEARTBEAT_PERIOD_MS));
    }
}

static BaseType_t prvFillResponseFromString(nualink_response_t *response,
                                            const char *json_line)
{
    size_t length;

    if ((response == NULL) || (json_line == NULL))
    {
        return pdFAIL;
    }

    length = strlen(json_line);
    if (length == 0U)
    {
        return pdFAIL;
    }
    if (length >= sizeof(response->data))
    {
        length = sizeof(response->data) - 1U;
    }

    memcpy(response->data, json_line, length);
    response->data[length] = '\0';
    response->length = (uint32_t)length;
    return pdPASS;
}

BaseType_t NuAILink_TasksPushNotification(const char *json_line)
{
    /* Use a file-scope static (guarded by a critical section) to avoid
     * placing a ~2 KB nualink_response_t on the caller's stack. */
    static nualink_response_t s_task_notify_buffer;
    BaseType_t result;

    if (s_response_queue == NULL)
    {
        return pdFAIL;
    }

    taskENTER_CRITICAL();
    if (prvFillResponseFromString(&s_task_notify_buffer, json_line) != pdPASS)
    {
        taskEXIT_CRITICAL();
        return pdFAIL;
    }
    result = xQueueSend(s_response_queue, &s_task_notify_buffer, 0U);
    taskEXIT_CRITICAL();
    return result;
}

BaseType_t NuAILink_TasksPushNotificationFromISR(const char *json_line,
                                                 BaseType_t *higher_priority_task_woken)
{
    /* ISR-side static buffer.  The GPB ISR cannot preempt itself, and other
     * ISRs running at <= the same priority never reach this code, so a single
     * shared static buffer is safe and avoids putting 2 KB on MSP. */
    static nualink_response_t s_isr_notify_buffer;

    if (s_response_queue == NULL)
    {
        return pdFAIL;
    }
    if (prvFillResponseFromString(&s_isr_notify_buffer, json_line) != pdPASS)
    {
        return pdFAIL;
    }
    return xQueueSendFromISR(s_response_queue, &s_isr_notify_buffer, higher_priority_task_woken);
}

BaseType_t NuAILink_TasksCreate(void)
{
    NUALINK_LOG("[SYS] creating queues/tasks...\n");

    s_request_queue = xQueueCreateStatic(NUALINK_REQUEST_QUEUE_LENGTH,
                                         sizeof(nualink_request_t),
                                         s_request_queue_storage,
                                         &s_request_queue_control);
    s_response_queue = xQueueCreateStatic(NUALINK_RESPONSE_QUEUE_LENGTH,
                                          sizeof(nualink_response_t),
                                          s_response_queue_storage,
                                          &s_response_queue_control);

    if ((s_request_queue == NULL) || (s_response_queue == NULL))
    {
        NUALINK_ERR("[SYS] queue create failed\n");
        return pdFAIL;
    }

    if (xTaskCreateStatic(prvUsbCommTask,
                          "USB_Comm",
                          (uint32_t)(sizeof(s_usb_task_stack) / sizeof(s_usb_task_stack[0])),
                          NULL,
                          configMAX_PRIORITIES - 2U,
                          s_usb_task_stack,
                          &s_usb_task_tcb) == NULL)
    {
        NUALINK_ERR("[SYS] USB_Comm task create failed\n");
        return pdFAIL;
    }

    if (xTaskCreateStatic(prvParserTask,
                          "MCP_Parse",
                          (uint32_t)(sizeof(s_parser_task_stack) / sizeof(s_parser_task_stack[0])),
                          NULL,
                          configMAX_PRIORITIES - 3U,
                          s_parser_task_stack,
                          &s_parser_task_tcb) == NULL)
    {
        NUALINK_ERR("[SYS] MCP_Parse task create failed\n");
        return pdFAIL;
    }

    if (xTaskCreateStatic(prvHeartbeatTask,
                          "Heartbeat",
                          (uint32_t)(sizeof(s_heartbeat_task_stack) / sizeof(s_heartbeat_task_stack[0])),
                          NULL,
                          1U,
                          s_heartbeat_task_stack,
                          &s_heartbeat_task_tcb) == NULL)
    {
        NUALINK_ERR("[SYS] Heartbeat task create failed\n");
        return pdFAIL;
    }

    NUALINK_LOG("[SYS] queues/tasks created OK\n");

    /* Phase 2.1: enable PB14 button input + edge interrupt; the ISR pushes
     * button.event JSON-RPC notifications into the response queue, which the
     * USB comm task forwards to the host. */
    NuAILink_BoardButtonInit();

    return pdPASS;
}