#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include <stdint.h>
#include <stdio.h>

#ifndef APP_TASK_STACK_SIZE
#  define APP_TASK_STACK_SIZE 4096
#endif

extern int main(int argc, char* argv[]);

extern uint32_t _init_array_start;
extern uint32_t _init_array_end;

static void call_init_array(void) {
    typedef void (*fn_t)(void);
    fn_t *p = (fn_t*)&_init_array_start;
    fn_t *e = (fn_t*)&_init_array_end;
    while (p < e) (*p++)();
}

static void app_main_task(void* param) {
    (void)param;
    call_init_array();
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
