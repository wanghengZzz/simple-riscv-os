#include "uart.h"
#include "mutex.h"

static kmutex_t uart_mutex = KMUTEX_INIT;

void uart_init(void) {
  writeb(UART_ADD(UART_IER), 0x00);
  writeb(UART_ADD(UART_LCR), UART_LCR_DLAB);
  writeb(UART_ADD(UART_DLL), UART_DIVISOR & 0xff);
  writeb(UART_ADD(UART_DLH), (UART_DIVISOR >> 8) & 0xff);
  writeb(UART_ADD(UART_LCR), 0x03);
  writeb(UART_ADD(UART_FCR), 0xC7);
  writeb(UART_ADD(UART_MCR), 0x03);
}

static void uart_putc_nolock(char c) {
  if (c == '\n') uart_putc_nolock('\r');
  while (!(readb(UART_ADD(UART_LSR)) & UART_LSR_THRE));
  writeb(UART_ADD(UART_THR), c);
}

void uart_putc(char c) {
  mutex_lock(&uart_mutex);
  uart_putc_nolock(c);
  mutex_unlock(&uart_mutex);
}

void uart_puts(const char* s) {
  mutex_lock(&uart_mutex);
  while (*s) uart_putc_nolock(*s++);
  mutex_unlock(&uart_mutex);
}

void uart_puts_nolock(const char* s) {
  while (*s) uart_putc_nolock(*s++);
}

char uart_getc(void) {
  mutex_lock(&uart_mutex);
  while (!(readb(UART_ADD(UART_LSR)) & UART_LSR_DR));
  char c = readb(UART_ADD(UART_RBR));
  mutex_unlock(&uart_mutex);
  return c;
}

static void print_uint(uint64_t val, uint32_t base) {
  char buf[64];
  int i = 0;
  if (val == 0) {
    uart_putc_nolock('0');
    return;
  }

  while (val > 0) {
    uint32_t rem = val % base;
    buf[i++] = rem < 10 ? '0' + rem : 'a' + rem - 10;
    val /= base;
  }

  while (i--) uart_putc_nolock(buf[i]);
}

void uart_printf(const char* fmt, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, fmt);

  mutex_lock(&uart_mutex);
  while (*fmt) {
    if (*fmt != '%') {
      uart_putc_nolock(*fmt++);
      continue;
    }
    fmt++;  // skip '%'
    int long_flag = 0;
    if (*fmt == 'l') {
      long_flag = 1;
      fmt++;
    }

    switch (*fmt++) {
      case 'c':
        uart_putc_nolock((char)__builtin_va_arg(args, int));
        break;
      case 's':
        uart_puts_nolock(__builtin_va_arg(args, const char*));
        break;
      case 'd': {
        intptr_t v = long_flag ? __builtin_va_arg(args, long)
                               : __builtin_va_arg(args, int);
        if (v < 0) {
          uart_putc_nolock('-');
          v = -v;
        }
        print_uint((uint64_t)v, 10);
        break;
      }
      case 'u':
        print_uint(__builtin_va_arg(args, uintptr_t), 10);
        break;
      case 'x':
        uart_puts_nolock("0x");
        print_uint(__builtin_va_arg(args, uintptr_t), 16);
        break;
      case '%':
        uart_putc_nolock('%');
        break;
    }
  }
  mutex_unlock(&uart_mutex);
  __builtin_va_end(args);
}
