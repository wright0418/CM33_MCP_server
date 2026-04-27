/*
 * FreeRTOS Kernel V11.1.0
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * This is a simple main that will start the FreeRTOS-Kernel and run a periodic task
 * that only delays if compiled with the template port, this project will do nothing.
 * For more information on getting started please look here:
 * https://freertos.org/FreeRTOS-quick-start-guide.html
 */

#include <FreeRTOS.h>
#include <task.h>

#include <stdio.h>

#include "NuMicro.h"
#include "cJSON.h"
#include "nualink_board.h"
#include "nualink_log.h"
#include "nualink_tasks.h"

/*-----------------------------------------------------------*/

#if (NUALINK_ENABLE_BOOT_DIAGNOSTICS == 1)
/*
 * Early-boot LED probe (no RTOS, no peripherals).
 *
 * Pattern after reset (PC14 LED, active-low on most NuMaker boards):
 *   - 3 quick blinks  : main() entered (CPU + vector table OK)
 *   - 6 quick blinks  : NuAILink_BoardInit() returned (clock/USBPHY OK)
 *   - 9 quick blinks  : NuAILink_TasksCreate() returned pdPASS
 *   - solid ON forever: NuAILink_TasksCreate() FAILED
 *   - heartbeat 2 Hz  : RTOS scheduler running normally
 *
 * Use only minimal CMSIS register access here so this works even before
 * BoardInit (unknown clock state -> we just toggle GPIO with crude delay).
 */
static void prv_BootProbe_InitLed(void)
{
    /* Make sure GPIOC clock is on (HIRC default after reset is enough). */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPCCKEN_Msk;
    GPIO_SetMode(PC, BIT14, GPIO_MODE_OUTPUT);
    PC14 = 1U; /* OFF (active low) */
}

static void prv_BootProbe_CrudeDelay(volatile uint32_t loops)
{
    while (loops--)
    {
        __NOP();
    }
}

static void prv_BootProbe_Blink(uint32_t count)
{
    uint32_t i;
    for (i = 0U; i < count; i++)
    {
        PC14 = 0U; /* ON  */
        prv_BootProbe_CrudeDelay(600000U);
        PC14 = 1U; /* OFF */
        prv_BootProbe_CrudeDelay(600000U);
    }
    prv_BootProbe_CrudeDelay(2000000U); /* gap between groups */
}

#define NUALINK_BOOT_PROBE_INIT() prv_BootProbe_InitLed()
#define NUALINK_BOOT_PROBE_BLINK(cnt) prv_BootProbe_Blink((cnt))
#else
#define NUALINK_BOOT_PROBE_INIT() ((void)0)
#define NUALINK_BOOT_PROBE_BLINK(cnt) ((void)(cnt))
#endif

static void prv_FatalHalt(void)
{
    PC14 = 0U;
    __disable_irq();

    for (;;)
    {
    }
}

void vAssertCalled(const char *pcFile,
                   uint32_t ulLine)
{
    NUALINK_ERR("[ASSERT] %s:%lu\n", (pcFile != NULL) ? pcFile : "<null>", (unsigned long)ulLine);
    NUALINK_ERR("[ASSERT] VTOR=0x%08lX SVC@VT=0x%08lX PendSV@VT=0x%08lX\n",
                (unsigned long)SCB->VTOR,
                (unsigned long)((uint32_t *)SCB->VTOR)[11],
                (unsigned long)((uint32_t *)SCB->VTOR)[14]);
    prv_FatalHalt();
}

static void prv_PrintFaultRegs(const char *fault_name)
{
    NUALINK_ERR("[FAULT] %s HFSR=0x%08lX CFSR=0x%08lX MMFAR=0x%08lX BFAR=0x%08lX ICSR=0x%08lX\n",
                (fault_name != NULL) ? fault_name : "<null>",
                (unsigned long)SCB->HFSR,
                (unsigned long)SCB->CFSR,
                (unsigned long)SCB->MMFAR,
                (unsigned long)SCB->BFAR,
                (unsigned long)SCB->ICSR);
}

void HardFault_Handler(void)
{
    prv_PrintFaultRegs("HardFault");
    prv_FatalHalt();
}

void MemManage_Handler(void)
{
    prv_PrintFaultRegs("MemManage");
    prv_FatalHalt();
}

void BusFault_Handler(void)
{
    prv_PrintFaultRegs("BusFault");
    prv_FatalHalt();
}

void UsageFault_Handler(void)
{
    prv_PrintFaultRegs("UsageFault");
    prv_FatalHalt();
}

int main(void)
{
    /* Probe 1: CPU reached main(). */
    SYS_UnlockReg();
    NUALINK_BOOT_PROBE_INIT();
    SYS_LockReg();
    NUALINK_BOOT_PROBE_BLINK(3U);

    NuAILink_BoardInit();
    /* Probe 2: clock + USBPHY init returned without hanging. */
    NUALINK_BOOT_PROBE_BLINK(6U);

    NuAILink_CJSONInitHooks();

    NUALINK_LOG("NuAILink FreeRTOS Project\n");
    NUALINK_LOG("cJSON version: %s\n", cJSON_Version());
    NUALINK_LOG("[SYS] calling NuAILink_TasksCreate()...\n");

    if (NuAILink_TasksCreate() != pdPASS)
    {
        NUALINK_ERR("NuAILink task bootstrap failed\n");
        /* Probe FAIL: stay solid ON. */
        PC14 = 0U;
        for (;;)
        {
        }
    }

    NUALINK_LOG("[SYS] NuAILink_TasksCreate() OK\n");

    /* Probe 3: tasks created OK; about to start scheduler. */
    NUALINK_BOOT_PROBE_BLINK(9U);
    NUALINK_LOG("[SYS] calling vTaskStartScheduler()...\n");

    /* Start the scheduler. */
    vTaskStartScheduler();

    /* If we get here, scheduler returned (out of heap or static memory hooks
     * missing).  Stay solid ON to flag failure. */
    NUALINK_ERR("[ERR] vTaskStartScheduler returned unexpectedly\n");
    PC14 = 0U;
    for (;;)
    {
        /* Should not reach here. */
    }
}
/*-----------------------------------------------------------*/

#if (configCHECK_FOR_STACK_OVERFLOW > 0)

void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                   char *pcTaskName)
{
    /* Check pcTaskName for the name of the offending task,
     * or pxCurrentTCB if pcTaskName has itself been corrupted. */
    NUALINK_ERR("[ERR] Stack overflow in task: %s\n", (pcTaskName != NULL) ? pcTaskName : "<null>");
    (void)xTask;
    PC14 = 0U;

    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

#endif /* #if ( configCHECK_FOR_STACK_OVERFLOW > 0 ) */
/*-----------------------------------------------------------*/

#if (configUSE_MALLOC_FAILED_HOOK == 1)

void vApplicationMallocFailedHook(void)
{
    NUALINK_ERR("[ERR] Malloc failed\n");
    PC14 = 0U;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

#endif /* #if ( configUSE_MALLOC_FAILED_HOOK == 1 ) */
/*-----------------------------------------------------------*/

#if (configSUPPORT_STATIC_ALLOCATION == 1)

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if (configUSE_TIMERS == 1)
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS */

#endif /* configSUPPORT_STATIC_ALLOCATION */
       /*-----------------------------------------------------------*/