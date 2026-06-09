/*
 * packet.c
 *
 *  Created on: 2026. 6. 8.
 *      Author: henry
 */

#include "packet.h"
#include "uart.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>


/* CRC16 */
uint16_t crc16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for(uint16_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for(uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021
                                 : (crc << 1);
    }
    return crc;
}

/* 패킷 생성 */
/* END 패킷도 262바이트로 통일 */
void make_packet(bl_data_packet_t *pkt,
                 uint8_t msg_type,
                 uint8_t *data,
                 uint16_t data_len)
{
    pkt->start_code  = START_CODE;
    pkt->msg_type    = msg_type;
    pkt->data_length = data_len;

    /* data 없으면 0xFF 패딩 */
    memset(pkt->data, 0xFF, PAGE_SIZE);
    if(data && data_len > 0)
        memcpy(pkt->data, data, data_len);

    /* CRC 계산 범위: MSG_TYPE + DATA_LENGTH + DATA(256) */
    uint8_t crc_buf[3 + PAGE_SIZE];
    crc_buf[0] = pkt->msg_type;
    crc_buf[1] = (uint8_t)(data_len & 0xFF);
    crc_buf[2] = (uint8_t)(data_len >> 8);
    for(uint16_t i = 0; i < PAGE_SIZE; i++)
        crc_buf[3 + i] = pkt->data[i];

    pkt->crc16 = crc16(crc_buf, 3 + PAGE_SIZE);
}


/* 패킷 전송 + ACK 수신 */
int send_packet(bl_data_packet_t *pkt)
{
    int pkt_size = 262;

    for(int retry = 0; retry < MAX_RETRY; retry++) {

        feedback = -1;

        /* SIGIO 끄기 */
        fcntl(uart_fd, F_SETFL, 0);

        /* 전송 */
        int w = write(uart_fd, pkt, pkt_size);
        tcdrain(uart_fd);
        printf("  write: %d bytes\n", w);

        /* SIGIO 켜기 */
        fcntl(uart_fd, F_SETFL, O_ASYNC);
        printf("  ACK 대기...\n");

        /* ACK 대기 (5초) */
        int timeout = 50000;
        while(timeout--) {
            if(feedback ==  1) {
                printf("  ACK ✅\n");
                return 1;
            }
            if(feedback ==  0) {
                printf("  NAK → 재전송\n");
                break;
            }
            usleep(100);
        }

        printf("  타임아웃 → retry %d/%d\n",
               retry+1, MAX_RETRY);
    }
    return 0;
}


int upload(int bin_fd, int page_count)
{
    bl_data_packet_t pkt;
    uint8_t page_buf[PAGE_SIZE];

    /*   페이지 전송 루프   */
    for(int i = 0; i < page_count; i++) {

        /* 256바이트 읽기 */
        memset(page_buf, 0xFF, PAGE_SIZE);
        int n = read(bin_fd, page_buf, PAGE_SIZE);
        if(n < 0) { perror("read fail"); break; }

        /* 패킷 생성 */
        make_packet(&pkt, MSG_DATA, page_buf, PAGE_SIZE);

        /* 전송 */
        if(!send_packet(&pkt)) {
            printf("  [%d/%d] 실패 ✗\n", i+1, page_count);
            close(bin_fd);
            close(uart_fd);
            return 1;
        }
        printf("  [%d/%d]   addr: 0x%05X\n",
               i+1, page_count, i * PAGE_SIZE);
    }


    /*  END 패킷 전송  */
    printf("\n[3] END 전송\n");
    make_packet(&pkt, MSG_END, NULL, 0);

    if(send_packet(&pkt)){
        printf("    완료! MCU App 점프\n");
    	 return 1;
    }

    else
        printf("    END ACK 없음 ✗\n");
    	 return 0;

}
