#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                1
#define configTICK_RATE_HZ                  1000
#define configMAX_PRIORITIES                8
#define configMINIMAL_STACK_SIZE            PTHREAD_STACK_MIN
#define configTICK_TYPE_WIDTH_IN_BITS       TICK_TYPE_WIDTH_32_BITS
#define configMAX_TASK_NAME_LEN             16
#define configSUPPORT_DYNAMIC_ALLOCATION    1
#define configSUPPORT_STATIC_ALLOCATION     0
#define configTOTAL_HEAP_SIZE               (64 * 1024 * 1024)  /* heap_3 = malloc */

#define configUSE_MUTEXES                   1
#define configUSE_RECURSIVE_MUTEXES         1
#define configUSE_COUNTING_SEMAPHORES       1
#define configUSE_TASK_NOTIFICATIONS        1
#define configUSE_TIMERS                    1
#define configTIMER_TASK_PRIORITY           (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH            10
#define configTIMER_TASK_STACK_DEPTH        configMINIMAL_STACK_SIZE

#define configUSE_IDLE_HOOK                 0
#define configUSE_TICK_HOOK                 0
#define configCHECK_FOR_STACK_OVERFLOW      0
#define configUSE_MALLOC_FAILED_HOOK        0

#define INCLUDE_vTaskDelay                  1
#define INCLUDE_xTaskDelayUntil             1
#define INCLUDE_vTaskDelete                 1

#include <limits.h>                          /* PTHREAD_STACK_MIN */
#endif
