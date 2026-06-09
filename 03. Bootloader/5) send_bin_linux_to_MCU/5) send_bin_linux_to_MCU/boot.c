/*
 * boot.h
 *
 * Created: 2026-06-08 오전 11:03:25
 *  Author: USER
 */ 

#include "boot.h"
#include "protocol.h"
#include <avr/boot.h>
#include <avr/interrupt.h>


/* ── Flash Write ── */
void flash_page_write(uint32_t addr, uint8_t *buf)
{
	cli();

	/* 1. Erase */
	boot_page_erase(addr);
	while(SPMCSR & (1 << SPMEN));

	/* 2. Fill (Word 단위) */
	for(uint16_t i = 0; i < PAGE_SIZE; i += 2) {
		uint16_t word = buf[i] | ((uint16_t)buf[i+1] << 8);
		boot_page_fill(i, word);
	}

	/* 3. Write */
	boot_page_write(addr);
	while(SPMCSR & (1 << SPMEN));

	/* 4. RWW 재활성화 */
	boot_rww_enable();

	sei();
}



/* ── App 점프 ── */
void jump_to_app(void)
{
	/* 인터럽트 벡터 → App 영역 */
	cli();
	MCUCR = (1 << IVCE);	// interrupt vector change enable
	MCUCR = (0 << IVSEL);	// interrupt vector select

	/* UART 비활성화 */
	UCSR0B = 0x00;

	asm volatile("jmp 0x0");
}
