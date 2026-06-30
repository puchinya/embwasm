#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void uart_init(void);
void uart_putchar(char c);
void uart_puts(const char *s);
#ifdef __cplusplus
}
#endif
