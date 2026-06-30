#include <stdint.h>

extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _estack;
extern uint32_t _init_array_start;
extern uint32_t _init_array_end;

extern void freertos_start(void);

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);
void SVC_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

__attribute__((section(".isr_vector"), used))
const void * const g_vectors[] = {
    (void*)&_estack,   /* 0: Initial SP */
    Reset_Handler,     /* 1: Reset */
    Default_Handler,   /* 2: NMI */
    HardFault_Handler, /* 3: HardFault */
    Default_Handler,   /* 4: MemManage */
    Default_Handler,   /* 5: BusFault */
    Default_Handler,   /* 6: UsageFault */
    0, 0, 0, 0,        /* 7-10: Reserved */
    SVC_Handler,       /* 11: SVCall */
    Default_Handler,   /* 12: Debug Monitor */
    0,                 /* 13: Reserved */
    PendSV_Handler,    /* 14: PendSV */
    SysTick_Handler,   /* 15: SysTick */
    /* IRQ0-61 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 0-3 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 4-7 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 8-11 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 12-15 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 16-19 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 20-23 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 24-27 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 28-31 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 32-35 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 36-39 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 40-43 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 44-47 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 48-51 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 52-55 */
    Default_Handler, Default_Handler, Default_Handler, Default_Handler, /* 56-59 */
    Default_Handler, Default_Handler,                                   /* 60-61 */
};

static void call_init_array(void) {
    typedef void (*fn_t)(void);
    fn_t *p = (fn_t*)&_init_array_start;
    fn_t *e = (fn_t*)&_init_array_end;
    while (p < e) (*p++)();
}

void Reset_Handler(void) {
    uint32_t *src = &_sidata;
    uint32_t *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;

    dst = &_sbss;
    while (dst < &_ebss) *dst++ = 0;

    call_init_array();
    freertos_start();
    while (1) {}
}

void Default_Handler(void) { while (1) {} }

extern void uart_putchar(char c);
void HardFault_Handler(void) {
    const char *msg = "HardFault!\r\n";
    while (*msg) uart_putchar(*msg++);
    while (1) {}
}
