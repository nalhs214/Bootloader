/*
 * LED_blink.c
 *
 * Created: 2026-04-14 오전 8:59:07
 * Author : USER
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdio.h>
#define LED_DDR   DDRC
#define LED_PORT  PORTC
#define LED_ALL   0x0F   // PC0~PC3 (4개 LED)

int main(void)
{

	LED_DDR |= LED_ALL;
	
    /* Replace with your application code */
    while (1)
    {
		LED_PORT |= LED_ALL;
		_delay_ms(500);
		LED_PORT &= ~LED_ALL;
		_delay_ms(1000);
    }
	return 0;
}

