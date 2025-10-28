#include "uart_ee627.h"
#include "tm4c123gh6pm.h"

void UART_setup(void)
{
    SYSCTL_RCGCUART_R |= (1 << 0);   // Enable UART0 clock
    SYSCTL_RCGCGPIO_R |= (1 << 0);   // Enable GPIOA clock

    while ((SYSCTL_PRUART_R & 0x01) == 0);
    while ((SYSCTL_PRGPIO_R & 0x01) == 0);

    GPIO_PORTA_AFSEL_R |= 0x03;      // Alternate function on PA0, PA1
    GPIO_PORTA_PCTL_R &= ~0xFF;
    GPIO_PORTA_PCTL_R |= 0x11;
    GPIO_PORTA_DEN_R |= 0x03;
    GPIO_PORTA_AMSEL_R &= ~0x03;

    UART0_CTL_R &= ~0x01;            // Disable UART0 during config

    // Baud = 9600, clock = 16 MHz → IBRD = 104, FBRD = 11
    UART0_IBRD_R = 104;
    UART0_FBRD_R = 11;

    UART0_LCRH_R = (0x3 << 5);       // 8-bit, no parity, 1 stop bit
    UART0_CTL_R = (1 << 0) | (1 << 8) | (1 << 9); // Enable UART, TXE, RXE
}

void UART_send(uint8_t data)
{
    while ((UART0_FR_R & 0x20) != 0);  // Wait until TX FIFO not full
    UART0_DR_R = data;                 // Send data
}
