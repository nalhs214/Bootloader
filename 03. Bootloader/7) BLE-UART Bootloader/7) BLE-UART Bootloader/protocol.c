/*
 * protocol.c
 *
 * Created: 2026-06-08 오전 11:08:40
 *  Author: USER
 */ 


#include "protocol.h"
#include "uart.h"



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


/*
 * msg_type으로 구분:
 *   MSG_DATA(0x01) → data_length = 256, data 있음
 *   MSG_END (0x02) → data_length = 0,   data 없음
 */
/* ── 262바이트 수신 후 파싱 ── */
uint8_t recv_packet(bl_data_packet_t *pkt)
{
    uint8_t raw[PKT_SIZE];
	
	while(!USART1_Available());
	do {
		raw[0] = USART1_Receive();
	}while(raw[0] != START_CODE);
	
    /* Step 1. 262바이트 전부 수신 */
    for(uint16_t i = 1; i < PKT_SIZE; i++){
        raw[i] = USART1_Receive();
	}
	

    /* Step 1-1. 262바이트 전부 수신 로그 출력 */	
    //for(uint16_t i = 0; i < PKT_SIZE; i++){
		//USART0_Transmit(hex[raw[i] >> 4]);
		//USART0_Transmit(hex[raw[i] & 0xF]);
		//USART0_Transmit(' ');
    //}
	//USART0_Transmit('|');           // 구분자
	//USART0_Transmit(hex[raw[261] >> 4]);
	//USART0_Transmit(hex[raw[261] & 0xF]);
	//USART0_Transmit('|');	
	//
	//USART0_Transmit('\r');
	//USART0_Transmit('\n');	
	
	
    /* Step 2. START_CODE 확인 */
    //if(raw[0] != START_CODE)
        //return NAK;

    /* Step 3. 파싱 */
	// data_length = little endian
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