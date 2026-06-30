#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include <stdio.h>

#ifndef APP_TASK_STACK_SIZE
#  define APP_TASK_STACK_SIZE 4096
#endif

extern int main(int argc, char* argv[]);

static void app_main_task(void* param) {
    (void)param;
    main(0, NULL);
    vTaskDelete(NULL);
}

void freertos_start(void) {
    uart_init();
    xTaskCreate(app_main_task, "main", APP_TASK_STACK_SIZE, NULL, 2, NULL);
    vTaskStartScheduler();
    while (1) {}
}

void vApplicationMallocFailedHook(void) {
    printf("MALLOC FAILED\r\n"); while (1) {}
}
void vApplicationStackOverflowHook(TaskHandle_t t, char* n) {
    (void)t; (void)n; printf("STACK OVERFLOW\r\n"); while (1) {}
}
void vApplicationIdleHook(void) {}
