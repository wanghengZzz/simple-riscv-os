#ifndef UART_H_
#define UART_H_

#include "types.h"

/* DTS: serial@10000000, ns16550a */
#define UART_BASE 0x10000000UL

#define UART_RBR 0 /* R:   Receiver Buffer Register            */
#define UART_THR 0 /* W:   Transmitter Holding Register        */
#define UART_IER 1 /* R/W: Interrupt Enable Register           */
#define UART_FCR 2 /* W:   FIFO Control Register               */
#define UART_LCR 3 /* R/W: Line Control Register               */
#define UART_MCR 4 /* R/W: Modem Control Register              */
#define UART_LSR 5 /* R:   Line Status Register                */
#define UART_DLL 0 /* W:   Divisor Latch Low (when DLAB=1)     */
#define UART_DLH 1 /* W:   Divisor Latch High (when DLAB=1)    */

#define UART_LSR_DR   (1 << 0) /* Data Ready                       */
#define UART_LSR_THRE (1 << 5) /* Transmitter Holding Register Empty */
#define UART_LCR_DLAB (1 << 7) /* Divisor Latch Access Bit         */

#define UART_CLK 3686400
#define UART_BAUD 115200
#define UART_DIVISOR (UART_CLK / (16 * UART_BAUD))
#define UART_ADD(offset) (UART_BASE + (offset))

static inline void writeb(uintptr_t addr, uint8_t val) { REG8(addr) = val; }

static inline uint8_t readb(uintptr_t addr) { return REG8(addr); }

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
char uart_getc(void);
void uart_printf(const char* fmt, ...);

#endif