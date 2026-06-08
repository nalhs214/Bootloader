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



/* ── 프로토콜 정의 ── */
#define START_CODE   0xEF
#define MSG_DATA     0x01
#define MSG_END      0x02
#define ACK          0x06
#define NAK          0x15
#define PAGE_SIZE    256
#define PKT_SIZE	 262  //1+1+2+256+2
#define BOOT_START   0x1E000UL



/* DATA 패킷 구조체 (262바이트)
 * __attribute__((packed)): 패딩 없이 1바이트 단위 정렬
 * → sizeof(bl_data_packet_t) = 262 정확히
 */
typedef struct {
    uint8_t  start_code;        // 0xEF
    uint8_t  msg_type;          // 0x01 = DATA, 0x02 = end
    uint16_t data_length;       // 256 (little-endian)
    uint8_t  data[PAGE_SIZE];   // bin 데이터 256바이트
    uint16_t crc16;             // CRC16 (little-endian)
} __attribute__((packed)) bl_data_packet_t;




/* ── CRC16 계산 ── */
/*
 * Polynomial: 0x1021
 * Init:       0xFFFF
 * 계산 범위:  MSG_TYPE + DATA_LENGTH + DATA
 */
uint16_t crc16(uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFF;
	for (uint16_t i = 0; i < len; i++) {
		crc ^= ((uint16_t)data[i] << 8);
		for (uint8_t j = 0; j < 8; j++) {
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
		}
	}
	return crc;
}



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
	MCUCR = (1 << IVCE);
	MCUCR = 0x00;

	/* UART 비활성화 */
	UCSR0B = 0x00;

	asm volatile("jmp 0x0");
}



/*
 * msg_type으로 구분:
 *   MSG_DATA(0x01) → data_length = 256, data 있음
 *   MSG_END (0x02) → data_length = 0,   data 없음
 */
/* ── 262바이트 수신 후 파싱 ── */
uint8_t recv_packet(bl_data_packet_t *pkt)
{
    uint8_t raw[PKT_SIZE];

    /* Step 1. 262바이트 전부 수신 */
    for(uint16_t i = 0; i < PKT_SIZE; i++)
        raw[i] = USART_Receive();

    /* Step 2. START_CODE 확인 */
    if(raw[0] != START_CODE)
        return NAK;

    /* Step 3. 파싱 */
    pkt->start_code  = raw[0];
    pkt->msg_type    = raw[1];
    pkt->data_length = (uint16_t)raw[2]
                     | ((uint16_t)raw[3] << 8);

    for(uint16_t i = 0; i < PAGE_SIZE; i++){
        pkt->data[i] = raw[4 + i];
	}

    pkt->crc16 = (uint16_t)raw[260]
               | ((uint16_t)raw[261] << 8);

    /* Step 4. CRC 검증
     * 범위: MSG_TYPE(1) + DATA_LENGTH(2) + DATA(256)
     */
    uint8_t crc_buf[3 + PAGE_SIZE];
    crc_buf[0] = pkt->msg_type;
    crc_buf[1] = raw[2];
    crc_buf[2] = raw[3];
    for(uint16_t i = 0; i < PAGE_SIZE; i++)
        crc_buf[3 + i] = pkt->data[i];

    uint16_t calc = crc16(crc_buf, 3 + PAGE_SIZE);

    if(calc != pkt->crc16)
        return NAK;

    return pkt->msg_type;
}


int main(void)
{
	DDRC  = 0xFF;
	PORTC = 0x00;

	UART_init();
	/* start LED */
	for(uint8_t i = 0; i < 5; i++) {
		PORTC = 0x0F;
		_delay_ms(200);
		PORTC = 0x00; 
		_delay_ms(200);
	}

	USART_Transmit('R');

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
	return 0;
}