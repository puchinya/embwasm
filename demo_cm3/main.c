#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include <stdio.h>

extern void bench_task_entry(void *param);

uint32_t SystemCoreClock = 100000000UL;

int main(void) {
    uart_init();
    printf("=== embwasm Cortex-M3 Benchmark (FreeRTOS) ===\r\n");

    xTaskCreate(bench_task_entry, "bench", 4096, NULL, 2, NULL);
    vTaskStartScheduler();

    while (1) {}
    return 0;
}

void vApplicationMallocFailedHook(void) {
    printf("MALLOC FAILED\r\n");
    while (1) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    (void)pcTaskName;
    printf("STACK OVERFLOW\r\n");
    while (1) {}
}

void vApplicationIdleHook(void) {}
