/*
 * 2) Flash_Read_Write.c
 *
 * Created: 2026-06-01 오전 9:57:14
 * Author : USER
 * Flash memmory를 읽고 쓸 때에는 1page 단위 (256byte = 128word)
 */

#include <avr/io.h>
#include <avr/boot.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdbool.h>


#define PAGE_SIZE 256
#define TEST_ADDR  0x0000UL

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

void make_test_data(uint8_t *buf)
{
	for (uint16_t i = 0; i < PAGE_SIZE; i++) {
		buf[i] = (uint8_t)(i & 0xFF);
	}
}

int main(void)
{
	DDRC = 0xFF;
	
    /* Replace with your application code */
    for(uint8_t i = 0; i < 3; i++)
    {
		PORTC = 0x0F;
		_delay_ms(500);
		PORTC = 0x00;
		_delay_ms(500);
    }
	_delay_ms(500);
	
	uint8_t test_buf[PAGE_SIZE];
	make_test_data(test_buf);
	

	cli();
	boot_page_erase(TEST_ADDR);
	while(SPMCSR & (1 << SPMEN)); //SPM 실행 완료 0
	boot_rww_enable();
	sei();

	
		
	flash_page_write(TEST_ADDR, test_buf);
	
}

