#ifndef UART_EE627_H_
#define UART_EE627_H_

#include <stdint.h>

// Initializes UART0 with 9600 baud, 8 data bits, no parity, 1 stop bit
void UART_setup(void);

// Sends one byte via UART0
void UART_send(uint8_t data);

#endif /* UART_EE627_H_ */
