/*
 * uart.c
 */

#include "uart.h"

/* ── UART 초기설정 ── */
void UART_init(void)
{
	UBRR0H = 0;
	UBRR0L = 103;   // 16MHz에서 19200 Baud Rate 설정  9600 = 103

	UCSR0B = (1 << RXEN0) | (1 << TXEN0);      // receiver, transmitter enable
	UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);    // 8bit character size, no parity
}


/* ── 송신 완료될 때까지 대기 후 전송 ── */
void USART_Transmit(unsigned char data)
{
	/* 송신 버퍼가 빌 때까지 대기 */
	while (!(UCSR0A & (1 << UDRE0)));
	/* 송신 버퍼에 데이터 쓰기 */
	UDR0 = data;
}


/* ── 수신 완료될 때까지 대기 후 반환 ── */
unsigned char USART_Receive(void)
{
	/* 수신 완료될 때까지 대기 */
	while (!(UCSR0A & (1 << RXC0)));
	/* 수신 버퍼에서 데이터 읽기 */
	return UDR0;
}

/* ── 수신 데이터 있는지 확인 (논블로킹) ── */
int USART_Available(void)
{
	return (UCSR0A & (1 << RXC0));
}



/* ── printf → UART 연결 ── */
static int uart_putchar(char c, FILE *stream)
{
	USART_Transmit((unsigned char)c);
	return 0;
}
static FILE uart_stdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);

/* ── stdout을 uart로 연결 ── */
void UART_stdout_init(void)
{
	stdout = &uart_stdout;
}

