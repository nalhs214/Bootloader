/*
 * Bootloader_MemorySetting.c
 *
 * Created: 2026-05-28 오후 3:46:02
 * Author : USER
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#define APP_START 0x0000



int main()
{
	DDRC=0xFF;
	for(int i=0;i<10;i++)
	{
		PORTC=0xFF;
		_delay_ms(250);
		PORTC=0x00;
		_delay_ms(250);
	}
	asm volatile("jmp 0x0");
}