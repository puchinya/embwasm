#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Reasons the firmware can signal completion for. The Renode script
 * (renode/run_firmware.resc.in) watches sysbus.usart1 for the matching
 * "[RENODE_EXIT] ..." line and quits the emulation when one appears. */
typedef enum {
    RENODE_EXIT_OK = 0,
    RENODE_EXIT_HARDFAULT,
    RENODE_EXIT_STACK_OVERFLOW,
    RENODE_EXIT_MALLOC_FAILED,
} renode_exit_reason_t;

/* Emits the sentinel line on UART and then halts forever. Never returns.
 * Safe to call from fault handlers (does not touch the heap or scheduler). */
void renode_exit(renode_exit_reason_t reason);

#ifdef __cplusplus
}
#endif
