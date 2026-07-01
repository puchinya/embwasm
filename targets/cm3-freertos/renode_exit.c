#include "renode_exit.h"
#include "uart.h"

void renode_exit(renode_exit_reason_t reason) {
    switch (reason) {
        case RENODE_EXIT_HARDFAULT:
            uart_puts("[RENODE_EXIT] HARDFAULT\r\n");
            break;
        case RENODE_EXIT_STACK_OVERFLOW:
            uart_puts("[RENODE_EXIT] STACK_OVERFLOW\r\n");
            break;
        case RENODE_EXIT_MALLOC_FAILED:
            uart_puts("[RENODE_EXIT] MALLOC_FAILED\r\n");
            break;
        case RENODE_EXIT_OK:
        default:
            uart_puts("[RENODE_EXIT] OK\r\n");
            break;
    }
    for (;;) {}
}
