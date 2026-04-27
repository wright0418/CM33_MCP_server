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

#include "cJSON.h"
#include "nualink_board.h"
#include "nualink_tasks.h"

/*-----------------------------------------------------------*/

int main( void )
{
    NuAILink_BoardInit();
    NuAILink_CJSONInitHooks();

    ( void ) printf( "NuAILink FreeRTOS Project\n" );
    ( void ) printf( "cJSON version: %s\n", cJSON_Version() );

    if( NuAILink_TasksCreate() != pdPASS )
    {
        ( void ) printf( "NuAILink task bootstrap failed\n" );
        for( ; ; )
        {
        }
    }

    /* Start the scheduler. */
    vTaskStartScheduler();

    for( ; ; )
    {
        /* Should not reach here. */
    }

}
/*-----------------------------------------------------------*/

#if ( configCHECK_FOR_STACK_OVERFLOW > 0 )

    void vApplicationStackOverflowHook( TaskHandle_t xTask,
                                        char * pcTaskName )
    {
        /* Check pcTaskName for the name of the offending task,
         * or pxCurrentTCB if pcTaskName has itself been corrupted. */
        ( void ) xTask;
        ( void ) pcTaskName;
    }

#endif /* #if ( configCHECK_FOR_STACK_OVERFLOW > 0 ) */
/*-----------------------------------------------------------*/

#if ( configUSE_MALLOC_FAILED_HOOK == 1 )

    void vApplicationMallocFailedHook( void )
    {
        taskDISABLE_INTERRUPTS();
        for( ; ; )
        {
        }
    }

#endif /* #if ( configUSE_MALLOC_FAILED_HOOK == 1 ) */
/*-----------------------------------------------------------*/

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

    void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer,
                                        StackType_t ** ppxIdleTaskStackBuffer,
                                        configSTACK_DEPTH_TYPE * pulIdleTaskStackSize )
    {
        static StaticTask_t xIdleTaskTCB;
        static StackType_t uxIdleTaskStack[ configMINIMAL_STACK_SIZE ];

        *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
        *ppxIdleTaskStackBuffer = uxIdleTaskStack;
        *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
    }

    #if ( configUSE_TIMERS == 1 )
        void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTimerTaskTCBBuffer,
                                             StackType_t ** ppxTimerTaskStackBuffer,
                                             configSTACK_DEPTH_TYPE * pulTimerTaskStackSize )
        {
            static StaticTask_t xTimerTaskTCB;
            static StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

            *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
            *ppxTimerTaskStackBuffer = uxTimerTaskStack;
            *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
        }
    #endif /* configUSE_TIMERS */

#endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/