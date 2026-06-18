/*
 * 7) BLE-UART Bootloader.c
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
	while(1){
		uint8_t result = recv_packet(&pkt);
		_delay_ms(100);
		
		if(result == MSG_DATA) {
			if(write_addr >= BOOT_START) {
				USART1_Transmit(NAK);
				break;
			}
			flash_page_write(write_addr, pkt.data);
			USART1_Transmit(ACK);
			write_addr += PAGE_SIZE;
			PORTC ^= 0x01;
		}
		
		else if(result == MSG_END) {
			USART1_Transmit(ACK);
			_delay_ms(100);
			jump_to_app();
		}
		else {
			USART1_Transmit(NAK);
		}
	}
}


int main(void)
{
	DDRC  = 0xFF;
	PORTC = 0x00;
	UART0_init();
	UART1_init();

	MCUCR = (1 << IVCE);
	MCUCR = (1 << IVSEL);
    sei();

	/* boot time out */
	
//while(1){
	//if(USART1_Available()){
		//uint8_t c = USART1_Receive();
		///* HEX 두 자리로 출력 */
		//char hex[] = "0123456789ABCDEF";
		//USART0_Transmit(hex[c >> 4]);
		//USART0_Transmit(hex[c & 0xF]);
		//USART0_Transmit(' ');
		//
	//}
//}


	uint16_t timeout = 10000;
	unsigned char c = 0;
	while(timeout--){
		PORTC = 0x05;
		_delay_ms(10);

	// start 신호 확인
		if(USART1_Available()){
			c = USART1_Receive();
			if(c == 'S'){
				USART1_RxFlush();
				USART1_Transmit('R');
				USART1_FlushTx();
				run_bootloader();

			}
		}
	}
	
	return 0;


	
	/* time out over, app memory have code */
	/*
	if(pgm_read_word(0x0000) != 0xFFFF){

		for(uint8_t i = 0; i < 5; i++) {
			PORTC = 0x0F;
			_delay_ms(200);
			PORTC = 0x00;
			_delay_ms(200);
		}	
		
		jump_to_app();
	}
*/
	while(1);
	return 0;
}