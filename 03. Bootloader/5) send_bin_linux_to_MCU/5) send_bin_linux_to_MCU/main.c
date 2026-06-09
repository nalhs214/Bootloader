/*
 * 5) send_bin_linux_to_MCU.c
 *
 * Created: 2026-06-04 오후 4:17:59
 * Author : USER
 * 19200baudrate UART 부트로더
 * 프로토콜 START(1)|MSG_TYPE(1)|DATA_LENGTH(2)|DATA(256)|CRC(2)
 */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>
#include "uart.h"
#include "boot.h"
#include "protocol.h"

void run_bootloader(void)
{
	bl_data_packet_t pkt;
	uint32_t write_addr = 0x0000;
	
	while(1)
	{
		uint8_t result = recv_packet(&pkt);

		if(result == MSG_DATA) {
			if(write_addr >= BOOT_START) {
				USART_Transmit(NAK);
				break;
			}
			flash_page_write(write_addr, pkt.data);
			USART_Transmit(ACK);
			write_addr += PAGE_SIZE;
			PORTC ^= 0x01;
		}
		
		else if(result == MSG_END) {
			USART_Transmit(ACK);
			_delay_ms(100);
			jump_to_app();
		}
		else {
			USART_Transmit(NAK);
		}
	}	
	
}


int main(void)
{
	DDRC  = 0xFF;
	PORTC = 0x00;
	UART_init();

	/* Send Start Message */
	USART_Transmit('R');
	
	/* boot time out */
	uint16_t timeout = 3000;
	while(timeout--){
		PORTC = 0x05;
		_delay_ms(1);
		
		if(USART_Available()){
			/* uart signal */
			run_bootloader();
			return 0;
		}
	}
	
	
	/* time out over, app memory have code */
	if(pgm_read_word(0x0000) != 0xFFFF){

		for(uint8_t i = 0; i < 5; i++) {
			PORTC = 0x0F;
			_delay_ms(200);
			PORTC = 0x00;
			_delay_ms(200);
		}	
		
		jump_to_app();
	}
	
	while(1);
	return 0;
}