/*
 * 4. MCU to linux.c
 *
 * Created: 2026-06-02 오전 8:52:17
 * Author : USER
 * MCU 에서 Linux(vmware)로 데이터를 보내고, 받고를 할 수 있는지 테스트
 */

#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>

#include "uart.h"

int main(void)
{
	UART_init();
	
	while(1)
	{

        unsigned char c = USART_Receive();

        USART_Transmit(c);
		USART_Transmit('\n');
		USART_Transmit('\r');

/*
		USART_Transmit('H');
		USART_Transmit('e');
		USART_Transmit('l');
		USART_Transmit('l');
		USART_Transmit('o');
		USART_Transmit('\n');
		USART_Transmit('\r');
		USART_Transmit('y');
		USART_Transmit('\n');
		USART_Transmit('\r');
		_delay_ms(1000);		
*/

	}
}

