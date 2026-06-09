/*
 * uart.h
 *
 *  Created on: 2026. 6. 8.
 *      Author: henry
 */

#ifndef INC_UART_H_
#define INC_UART_H_

#include <stdint.h>
#include <termios.h>


extern int uart_fd;
extern volatile int8_t  feedback;  // -1=미수신 0=NAK 1=ACK
extern volatile uint8_t ready_recv;  // READY 수신 플래그


void sigio_handler(int sig);
int uart_open(const char *port);
int wait_ready(void);
void DTR_Reset(void);

#endif /* INC_UART_H_ */
