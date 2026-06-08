/*
 * uart.h
 */


#ifndef UART_H_
#define UART_H_

#include <avr/io.h>
#include <stdio.h>

void UART_init(void);
void UART_stdout_init(void);
void USART_Transmit(unsigned char data);
unsigned char USART_Receive(void);
int USART_Available(void);


#endif /* UART_H_ */