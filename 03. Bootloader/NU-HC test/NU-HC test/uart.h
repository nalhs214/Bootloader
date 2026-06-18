/*
 * uart.h
 */


#ifndef UART_H_
#define UART_H_

#include <avr/io.h>
#include <stdio.h>
void UART0_init(void);
void UART1_init(void);
void USART0_Transmit(unsigned char data);
void USART1_Transmit(unsigned char data);
unsigned char USART0_Receive(void);
unsigned char USART1_Receive(void);
int  USART0_Available(void);
int  USART1_Available(void);
void UART_stdout_init(void);


#endif /* UART_H_ */