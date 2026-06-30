#include "uart.h"
#include <stdint.h>

#define USART1_BASE  0x40013800U
#define USART1_SR   (*(volatile uint32_t*)(USART1_BASE + 0x00U))
#define USART1_DR   (*(volatile uint32_t*)(USART1_BASE + 0x04U))
#define USART1_CR1  (*(volatile uint32_t*)(USART1_BASE + 0x0CU))

#define USART_SR_TXE   (1U << 7)
#define USART_CR1_UE   (1U << 13)
#define USART_CR1_TE   (1U << 3)

void uart_init(void) {
    USART1_CR1 = USART_CR1_UE | USART_CR1_TE;
}

void uart_putchar(char c) {
    while (!(USART1_SR & USART_SR_TXE)) {}
    USART1_DR = (uint32_t)(unsigned char)c;
}

void uart_puts(const char *s) {
    while (*s) uart_putchar(*s++);
}
