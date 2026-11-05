/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Physical original-ESP32 CCOUNT capture, NOT a simulator timing model.
 * IRAM kernel blocks include LOOP/RSYNC/DSYNC/counter overhead. CPU cores run
 * sequentially, maskable interrupts disabled only inside each measurement.
 * Peer idle tasks, NMI/debug, power and bus effects are not fully isolated.
 * No radio, GPIO, eFuse or flash programming is performed by app_main. */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_attr.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define REPEATS 17
#define COUNTS 6
#define CASES 9
static const uint32_t counts[COUNTS]={1,4,16,64,256,1024};
static const char *const names[CASES]={"nop","addi_dependent","mull_dependent","quou_independent","quou_dependent","l32i_dram","s32i_dram","l32ai_dram","s32c1i_match_pair"};
static uint32_t samples[2][2][COUNTS][CASES][REPEATS];
static uint32_t scratch[2];
static TaskHandle_t supervisor;
static portMUX_TYPE capture_lock=portMUX_INITIALIZER_UNLOCKED;
/* Volatile assembly is the measurement boundary. All cases share the framing,
 * but layout and operand dependencies remain part of the recorded kernel. */
#define MEASURE(body) do { \
    __asm__ volatile("dsync; rsr.ccount %[start]; rsync; loop %[n],1f\n" \
                     ".begin no-transform\n" body "\n.end no-transform\n1: dsync; rsr.ccount %[end]; rsync" \
                     : [start] "=&a"(start),[end] "=&a"(end),[value] "+a"(value) \
                     : [n] "a"(iterations),[address] "a"(address),[operand] "a"(operand) \
                     : "memory"); \
} while(0)
static uint32_t IRAM_ATTR __attribute__((noinline)) measure(unsigned kind,uint32_t iterations,uint32_t *address)
{
    uint32_t start,end,value=kind==8 ? 0u : UINT32_C(0x12345678),operand=3;
    switch(kind) {
    case 0:MEASURE("nop");break;
    case 1:MEASURE("addi %[value],%[value],1");break;
    case 2:MEASURE("mull %[value],%[value],%[operand]");break;
    case 3:MEASURE("quou %[value],%[n],%[operand]");break;
    case 4:MEASURE("quou %[value],%[value],%[operand]");break;
    case 5:MEASURE("l32i %[value],%[address],0");break;
    case 6:MEASURE("s32i %[value],%[address],0");break;
    case 7:MEASURE("l32ai %[value],%[address],0");break;
    default:MEASURE("wsr.scompare1 %[value]; s32c1i %[value],%[address],0");break;
    }
    return end-start; /* Defined modulo 2^32; each bounded kernel is short. */
}
static void capture(void *argument)
{
    unsigned cpu=(unsigned)(uintptr_t)argument,mode,c,k,r;
    for(mode=0;mode<2;mode++) for(c=0;c<COUNTS;c++) for(k=0;k<CASES;k++) for(r=0;r<REPEATS;r++) {
        uint32_t saved,selected,elapsed;
        scratch[cpu]=k==8 ? 0u : UINT32_C(0x12345678);
        portENTER_CRITICAL(&capture_lock);
        __asm__ volatile("rsr.memctl %0" : "=a"(saved));
        selected=(saved&~UINT32_C(1))|mode;
        __asm__ volatile("wsr.memctl %0; isync" :: "a"(selected) : "memory");
        elapsed=measure(k,counts[c],&scratch[cpu]);
        __asm__ volatile("wsr.memctl %0; isync" :: "a"(saved) : "memory");
        portEXIT_CRITICAL(&capture_lock);
        samples[cpu][mode][c][k][r]=elapsed;
    }
    xTaskNotifyGive(supervisor);vTaskDelete(NULL);
}
void app_main(void)
{
    unsigned cpu,mode,c,k,r;esp_chip_info_t chip;esp_chip_info(&chip);supervisor=xTaskGetCurrentTaskHandle();
    vTaskDelay(pdMS_TO_TICKS(250));
    for(cpu=0;cpu<2;cpu++) {
        if(xTaskCreatePinnedToCore(capture,"cycle_capture",4096,(void *)(uintptr_t)cpu,5,NULL,(BaseType_t)cpu)!=pdPASS) {
            puts("EMBSIM_TIMING {\"schema\":1,\"kind\":\"error\",\"reason\":\"task_creation\"}");return;
        }
        (void)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
    printf("EMBSIM_TIMING {\"schema\":1,\"kind\":\"context\",\"idf\":\"%s\",\"chip_model\":%d,\"revision_code\":%u,\"reported_cpu_hz\":%d,\"placement\":\"IRAM\",\"timing_model_qualified\":false}\n",
           IDF_VER,(int)chip.model,(unsigned)chip.revision,esp_clk_cpu_freq());
    for(cpu=0;cpu<2;cpu++) for(mode=0;mode<2;mode++) for(c=0;c<COUNTS;c++) for(k=0;k<CASES;k++) for(r=0;r<REPEATS;r++) {
        printf("EMBSIM_TIMING {\"schema\":1,\"kind\":\"sample\",\"core\":%u,\"loop_buffer\":%u,\"case\":\"%s\",\"iterations\":%" PRIu32 ",\"repeat\":%u,\"raw_cycles\":%" PRIu32 "}\n",
               cpu,mode,names[k],counts[c],r,samples[cpu][mode][c][k][r]);
        if(r==REPEATS-1) vTaskDelay(1);
    }
    puts("EMBSIM_TIMING {\"schema\":1,\"kind\":\"complete\",\"samples\":3672,\"timing_model_qualified\":false}");
}
