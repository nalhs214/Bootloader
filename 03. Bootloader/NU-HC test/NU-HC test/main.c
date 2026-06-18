/*
 * NU-HC test.c
 *
 * Created: 2026-06-17 오전 9:46:59
 * Author : USER
 */


#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include "uart.h"

int main(void)
{
	DDRC  = 0xFF;
	PORTC = 0x00;

	UART0_init();          // PC 시리얼
	UART1_init();          // NU-HC
	sei();                 // 전역 인터럽트 enable (RX/TX ISR 동작에 필수)

	/* 시작 표시 (UART0 터미널에서 확인) */
	USART0_Transmit('B');  // Bridge ready
	USART0_Transmit('\r');
	USART0_Transmit('\n');

	while(1)
	{
		/* UART0(PC) → UART1(NU-HC) : 받은 걸 BLE로 송신 */
		if(USART0_Available()){
			uint8_t c = USART0_Receive();
			USART1_Transmit(c);      // NU-HC로 전달 → BLE 송신
			PORTC ^= 0x01;           // 전달할 때마다 LED 토글
		}

		/* UART1(NU-HC) → UART0(PC) : BLE로 들어온 걸 터미널에 표시 (양방향 확인용) */
		if(USART1_Available()){
			uint8_t c = USART1_Receive();
			USART0_Transmit(c);
			PORTC ^= 0x02;
		}
	}
}

