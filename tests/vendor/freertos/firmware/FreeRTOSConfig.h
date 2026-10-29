/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* FreeRTOSConfig.h -- the task-awareness test on the NUCLEO-F446RE: the
 * core on HSI (16 MHz), a 1 ms tick, two notification indexes, stack
 * checking (which fills each stack with 0xA5, the high-water mark's
 * source) and the trace facility. */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ 16000000UL
#define configTICK_RATE_HZ 1000
#define configUSE_PREEMPTION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE 128
#define configTOTAL_HEAP_SIZE (16 * 1024)
#define configMAX_TASK_NAME_LEN 12
#define configUSE_16_BIT_TICKS 0
#define configUSE_MUTEXES 1
#define configUSE_TRACE_FACILITY 1
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 2
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configUSE_TIMERS 0
#define configQUEUE_REGISTRY_SIZE 4
#define INCLUDE_vTaskDelay 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1

#define configPRIO_BITS 4
#define configKERNEL_INTERRUPT_PRIORITY (15 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (5 << (8 - configPRIO_BITS))

#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif
