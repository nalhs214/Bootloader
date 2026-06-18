/*
 * protocol.h
 *
 * 
 *  
 */ 


#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>


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



uint16_t crc16(uint8_t *data, uint16_t len);
uint8_t recv_packet(bl_data_packet_t *pkt);


#endif /* PROTOCOL_H_ */