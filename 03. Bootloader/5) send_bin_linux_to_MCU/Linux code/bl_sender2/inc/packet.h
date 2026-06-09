/*
 * packet.h
 *
 *  Created on: 2026. 6. 8.
 *      Author: henry
 */

#ifndef INC_PACKET_H_
#define INC_PACKET_H_

#include <stdint.h>

#define START_CODE  0xEF
#define MSG_DATA    0x01
#define MSG_END     0x02
#define ACK	      0x06
#define NAK	      0x15
#define PAGE_SIZE   256
#define MAX_RETRY   3


typedef struct {
    uint8_t  start_code;			// 0xEF
    uint8_t  msg_type;			// 0x01, 0x02
    uint16_t data_length;		// 256(little-endian
    uint8_t  data[PAGE_SIZE];	// bin 256byte
    uint16_t crc16;
} __attribute__((packed)) bl_data_packet_t;


uint16_t crc16(uint8_t *data, uint16_t len);
void make_packet(bl_data_packet_t *pkt,
                 uint8_t msg_type,
                 uint8_t *data,
                 uint16_t data_len);
int send_packet(bl_data_packet_t *pkt);
int upload(int bin_fd, int page_count);

#endif /* INC_PACKET_H_ */
