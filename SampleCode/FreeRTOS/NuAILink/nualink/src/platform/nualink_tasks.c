#include "nualink_tasks.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "cJSON.h"
#include "mcp_jsonrpc.h"
#include "nualink_board.h"
#include "nualink_config.h"
#include "nualink_transport.h"

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
#pragma data_alignment=4
static uint8_t s_request_queue_storage[NUALINK_REQUEST_QUEUE_LENGTH * sizeof(nualink_request_t)];
#pragma data_alignment=4
static uint8_t s_response_queue_storage[NUALINK_RESPONSE_QUEUE_LENGTH * sizeof(nualink_response_t)];
#else
static uint8_t s_request_queue_storage[NUALINK_REQUEST_QUEUE_LENGTH * sizeof(nualink_request_t)] __attribute__((aligned(4)));
static uint8_t s_response_queue_storage[NUALINK_RESPONSE_QUEUE_LENGTH * sizeof(nualink_response_t)] __attribute__((aligned(4)));
#endif

static StaticTask_t s_usb_task_tcb;
static StaticTask_t s_parser_task_tcb;
static StaticTask_t s_heartbeat_task_tcb;
static StackType_t s_usb_task_stack[1024];
static StackType_t s_parser_task_stack[4096];
static StackType_t s_heartbeat_task_stack[512];

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
    if(text_length >= sizeof(response.data))
    {
        text_length = sizeof(response.data) - 1U;
    }

    memcpy(response.data, text, text_length);
    response.data[text_length] = '\0';
    response.length = (uint32_t)text_length;
    (void)xQueueSend(s_response_queue, &response, 0U);
}

static void prvUsbCommTask(void *parameters)
{
    uint8_t packet[NUALINK_USB_RX_PACKET_SIZE];
    nualink_request_t request;
    nualink_response_t response;
    bool discarding_oversized_message = false;

    (void)parameters;
    memset(&request, 0, sizeof(request));
    NuAILink_TransportInit();

    for(;;)
    {
        uint32_t packet_length;

        NuAILink_TransportPoll();

        while(xQueueReceive(s_response_queue, &response, 0U) == pdPASS)
        {
            (void)NuAILink_TransportWrite((const uint8_t *)response.data,
                                          response.length,
                                          pdMS_TO_TICKS(NUALINK_USB_TX_TIMEOUT_MS));
        }

        packet_length = NuAILink_TransportReadPacket(packet, sizeof(packet));
        if(packet_length > 0U)
        {
            uint32_t index;

            for(index = 0U; index < packet_length; index++)
            {
                uint8_t byte_value = packet[index];

                if(byte_value == '\r')
                {
                    continue;
                }

                if(byte_value == '\n')
                {
                    if(discarding_oversized_message)
                    {
                        discarding_oversized_message = false;
                        request.length = 0U;
                        prvQueueImmediateResponse("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32600,\"message\":\"message_too_large\"},\"id\":null}\n");
                    }
                    else if(request.length > 0U)
                    {
                        request.data[request.length] = '\0';
                        if(xQueueSend(s_request_queue, &request, 0U) != pdPASS)
                        {
                            prvQueueImmediateResponse("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32000,\"message\":\"server_busy\"},\"id\":null}\n");
                        }
                        request.length = 0U;
                    }
                    else
                    {
                        request.length = 0U;
                    }
                }
                else if(!discarding_oversized_message)
                {
                    if(request.length < (sizeof(request.data) - 1U))
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

        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}

static void prvParserTask(void *parameters)
{
    nualink_request_t request;
    nualink_response_t response;

    (void)parameters;

    for(;;)
    {
        if(xQueueReceive(s_request_queue, &request, portMAX_DELAY) == pdPASS)
        {
            int32_t length;

            response.length = 0U;
            response.data[0] = '\0';

            length = MCP_JSONRPC_Handle(request.data, response.data, sizeof(response.data));
            if(length > 0)
            {
                response.length = (uint32_t)length;
                (void)xQueueSend(s_response_queue, &response, pdMS_TO_TICKS(100U));
            }
        }
    }
}

static void prvHeartbeatTask(void *parameters)
{
    (void)parameters;

    for(;;)
    {
        NuAILink_BoardHeartbeatToggle();
        vTaskDelay(pdMS_TO_TICKS(NUALINK_HEARTBEAT_PERIOD_MS));
    }
}

BaseType_t NuAILink_TasksCreate(void)
{
    s_request_queue = xQueueCreateStatic(NUALINK_REQUEST_QUEUE_LENGTH,
                                         sizeof(nualink_request_t),
                                         s_request_queue_storage,
                                         &s_request_queue_control);
    s_response_queue = xQueueCreateStatic(NUALINK_RESPONSE_QUEUE_LENGTH,
                                          sizeof(nualink_response_t),
                                          s_response_queue_storage,
                                          &s_response_queue_control);

    if((s_request_queue == NULL) || (s_response_queue == NULL))
    {
        return pdFAIL;
    }

    if(xTaskCreateStatic(prvUsbCommTask,
                         "USB_Comm",
                         (uint32_t)(sizeof(s_usb_task_stack) / sizeof(s_usb_task_stack[0])),
                         NULL,
                         configMAX_PRIORITIES - 2U,
                         s_usb_task_stack,
                         &s_usb_task_tcb) == NULL)
    {
        return pdFAIL;
    }

    if(xTaskCreateStatic(prvParserTask,
                         "MCP_Parse",
                         (uint32_t)(sizeof(s_parser_task_stack) / sizeof(s_parser_task_stack[0])),
                         NULL,
                         configMAX_PRIORITIES - 3U,
                         s_parser_task_stack,
                         &s_parser_task_tcb) == NULL)
    {
        return pdFAIL;
    }

    if(xTaskCreateStatic(prvHeartbeatTask,
                         "Heartbeat",
                         (uint32_t)(sizeof(s_heartbeat_task_stack) / sizeof(s_heartbeat_task_stack[0])),
                         NULL,
                         1U,
                         s_heartbeat_task_stack,
                         &s_heartbeat_task_tcb) == NULL)
    {
        return pdFAIL;
    }

    return pdPASS;
}