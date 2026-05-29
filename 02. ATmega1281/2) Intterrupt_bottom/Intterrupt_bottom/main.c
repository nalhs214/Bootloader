/*

	버튼누르면 인터럽트 신호를 받아 Count
	Count한 숫자를 LED 2진 숫자 표현
	
 */ 


#define F_CPU       16000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include "uart.h"
#include "LED.h"
#include "gpio.h"

#define LED_DDR     DDRC
#define LED_PORT    PORTC
#define LED_MASK    0x0F        // PC0~PC3 (4개 LED)

volatile uint8_t count = 0;
volatile uint8_t flag = 0;



int main(void)
{
	LED_init();
	INT7_Init();
	UART_init();
	UART_stdout_init();
	
	SREG |= (1 << 7);	// 상태 레지스터, Global Intterrupt enable

	LED_display(0);	
	printf("Start!\r\n");


    while (1) 
    {
        /* ── INT7 버튼 카운트 출력 ── */
        if (flag)
        {
	        flag = 0;
	        printf("버튼 눌림! count = %d\r\n", count);
        }

        /* ── UART 수신 처리 (폴링) ── */
		if(USART_Available())
		{
			unsigned char c = USART_Receive();
			printf("수신 : %c\r\n", c);
			
			if (c=='1')
			{
				count = 0;
				LED_display(count);
				printf("count 초기화!\r\n");
			}
		}
    }
	return 0;
}

