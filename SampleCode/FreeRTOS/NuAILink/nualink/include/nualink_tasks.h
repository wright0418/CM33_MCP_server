#ifndef NUALINK_TASKS_H
#define NUALINK_TASKS_H

#include "FreeRTOS.h"

void NuAILink_CJSONInitHooks(void);
BaseType_t NuAILink_TasksCreate(void);

/*
 * Push a pre-formatted JSON-RPC line (must end with '\n') into the USB TX
 * response queue.  Used by plugins / ISRs to emit unsolicited notifications
 * (no "id" field) such as button.event.  Returns pdPASS on success.
 */
BaseType_t NuAILink_TasksPushNotification(const char *json_line);
BaseType_t NuAILink_TasksPushNotificationFromISR(const char *json_line,
                                                 BaseType_t *higher_priority_task_woken);

#endif /* NUALINK_TASKS_H */