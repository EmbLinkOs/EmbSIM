/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* main.c -- FreeRTOS tasks in each state the task awareness reports: one
 * delayed (vTaskDelay), one waiting on notification index 1 for ever, one
 * waiting on a queue, one suspended, and a notification pending on index 0
 * of the delayed task. "ready" prints on USART2 once they are made. */
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define RCC_AHB1ENR REG(0x40023830u)
#define RCC_APB1ENR REG(0x40023840u)
#define GPIOA_MODER REG(0x40020000u)
#define GPIOA_AFRL REG(0x40020020u)
#define USART2_SR REG(0x40004400u)
#define USART2_DR REG(0x40004404u)
#define USART2_BRR REG(0x40004408u)
#define USART2_CR1 REG(0x4000440Cu)

static QueueHandle_t q;
static TaskHandle_t sleeper;

static void puts2(const char *s)
{
    for (; *s; s++) {
        while (!(USART2_SR & (1u << 7)))
            ;
        USART2_DR = (uint8_t)*s;
    }
}

static void sleeper_task(void *p)
{
    (void)p;
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

static void waiter_task(void *p)
{
    (void)p;
    for (;;)
        (void)ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);
}

static void rx_task(void *p)
{
    uint32_t v;
    (void)p;
    for (;;)
        (void)xQueueReceive(q, &v, portMAX_DELAY);
}

static void parked_task(void *p)
{
    (void)p;
    vTaskSuspend(NULL);
    for (;;)
        ;
}

static void main_task(void *p)
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(5));
    (void)xTaskNotifyIndexed(sleeper, 0, 0x2Au, eSetValueWithOverwrite);
    puts2("ready\r\n");
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(250));
}

int main(void)
{
    RCC_AHB1ENR |= 1u;       /* GPIOA */
    RCC_APB1ENR |= 1u << 17; /* USART2 */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << 4)) | (2u << 4);
    GPIOA_AFRL = (GPIOA_AFRL & ~(0xFu << 8)) | (7u << 8); /* PA2: USART2_TX */
    USART2_BRR = 16000000u / 115200u;
    USART2_CR1 = (1u << 13) | (1u << 3);
    q = xQueueCreate(4, sizeof(uint32_t));
    vQueueAddToRegistry(q, "rxq");
    (void)xTaskCreate(sleeper_task, "sleeper", 160, NULL, 1, &sleeper);
    (void)xTaskCreate(waiter_task, "waiter", 160, NULL, 2, NULL);
    (void)xTaskCreate(rx_task, "rx", 160, NULL, 2, NULL);
    (void)xTaskCreate(parked_task, "parked", 128, NULL, 1, NULL);
    (void)xTaskCreate(main_task, "main", 200, NULL, 3, NULL);
    vTaskStartScheduler();
    for (;;)
        ;
}

void vApplicationStackOverflowHook(TaskHandle_t t, char *name)
{
    (void)t;
    puts2("stack overflow: ");
    puts2(name);
    puts2("\r\n");
    for (;;)
        ;
}
