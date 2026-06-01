/*
 * 3) Uart_Read_Write.c
 *
 * Created: 2026-06-01 오후
 * Author : USER
 * Uart로 데이터를 받아오고, 그 값을 flash에 write (256byte = 128word)
 * python으로 hex 파일을 bin 파일로 변환해서 입력해줌
 */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>
#include "uart.h"


#define PAGE_SIZE 256
#define TEST_ADDR_START  0x0000UL
#define END_SIGNAL 0xff
#define ACK 0x06

void flash_erase(uint32_t addr)
{
	boot_page_erase(addr);
	while(SPMCSR & (1 << SPMEN)); //SPM 실행 완료 0
	boot_rww_enable();
}

// 1byte 옆으로 밀어서 1word씩 넣기
void flash_fill(uint32_t addr, uint8_t *buf)
{
	for(uint16_t i = 0; i < PAGE_SIZE; i += 2){
		uint16_t word = buf[i] | ((uint16_t)buf[i+1] << 8);		
		boot_page_fill(addr + i, word);
	}
}

void flash_write(uint32_t addr)
{
	boot_page_write(addr);
	while(SPMCSR & (1 << SPMEN));
	//boot_spm_busy_wait();
	boot_rww_enable();
}

void flash_page_write(uint32_t addr, uint8_t *buf)
{
	cli();
	
	flash_erase(addr);
	while(SPMCSR & (1 << SPMEN));	
	
	flash_fill(addr, buf);
	flash_write(addr);
	
	sei();
}


bool recv_page(uint8_t *buf)
{
	/* 비어있는 값 오면 종료 */
	buf[0] = USART_Receive();

	if(buf[0] == END_SIGNAL)
	return false;

	/* 나머지 255바이트 수신 */
	for(uint16_t i = 1; i < PAGE_SIZE; i++) {
		buf[i] = USART_Receive();
	}
	return true;
}

int main(void)
{
	DDRC = 0xFF;
	UART_init();
	
    for(uint8_t i = 0; i < 3; i++)
    {
		PORTC = 0x0F;
		_delay_ms(500);
		PORTC = 0x00;
		_delay_ms(500);
    }

	uint8_t buf[PAGE_SIZE];
	uint32_t write_addr = TEST_ADDR_START;
	
    USART_Transmit('R');
    USART_Transmit('E');
    USART_Transmit('A');
    USART_Transmit('D');
    USART_Transmit('Y');
    USART_Transmit('\r');
    USART_Transmit('\n');	
	
	while(1)
	{
		if(!recv_page(buf)){
			USART_Transmit(ACK);
			break;
		}
		
        flash_page_write(write_addr, buf);
		USART_Transmit(ACK);
		
        write_addr += PAGE_SIZE;		
	}
	

	/* UART 비활성화 */
	UCSR0B = 0x00;

	/* 인터럽트 벡터 → App 영역으로 전환 */
	cli();
	MCUCR = (1 << IVCE);
	MCUCR = 0x00;

	/* App으로 점프 */
	asm volatile("jmp 0x0");
	
}

