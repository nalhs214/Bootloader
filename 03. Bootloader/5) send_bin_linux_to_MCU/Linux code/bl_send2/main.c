/*
 * main.c
 *
 *  Created on: 2026. 6. 5.
 *      Author: henry
 */


#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>

#define START_CODE  0xEF
#define MSG_DATA    0x01
#define MSG_END     0x02
#define ACK         0x06
#define NAK         0x15
#define PAGE_SIZE   256
#define MAX_RETRY   3

typedef struct {
    uint8_t  start_code;
    uint8_t  msg_type;
    uint16_t data_length;
    uint8_t  data[PAGE_SIZE];
    uint16_t crc16;
} __attribute__((packed)) bl_data_packet_t;

int uart_fd;
volatile uint8_t feedback = 0;

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


void sigio_handler(int sig)
{
    uint8_t resp = 0;
    int n = read(uart_fd, &resp, 1);

    if (n == 1){
        if(resp == ACK) feedback = 1;
        if(resp == NAK) feedback = 0;
        printf("  [SIGIO] feedback: 0x%02X\n", feedback);
    }
}


/* UART 설정 */
int uart_open(const char *port)
{
    int fd = open(port, O_RDWR);
    if(fd < 0) { perror("open fail"); return -1; }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cc[VTIME] = 20;   // 2초 타임아웃
    tty.c_cc[VMIN]  = 0;
    tcsetattr(fd, TCSANOW, &tty);

	/* SIGIO setting */
	signal(SIGIO, sigio_handler);	// SIGIO 들어오면 실행
	fcntl(fd, F_SETOWN, getpid());	// 현재 프로세스에게 신호보내기
	fcntl(fd, F_SETFL, O_ASYNC);	// 데이터 들어오면 자동으로 SIGIO 발생

    return fd;
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
    int pkt_size = 1 + 1 + 2 + pkt->data_length + 2;

    for(int retry = 0; retry < MAX_RETRY; retry++) {

    	/* 피드백 초기화 & SIGIO 초기화 */
    	feedback = 0;
    	fcntl(uart_fd, F_SETFL, 0);

        /* 전송 */
       write(uart_fd, pkt, pkt_size);
       tcdrain(uart_fd);   // TX 완료 대기

        /*
         * 9600 baud 262바이트 전송 시간:약 273ms
        */
       usleep(300000);             // 300ms 대기
       tcflush(uart_fd, TCIFLUSH); // echo 버리기

       /* ACK 수신 (블로킹) */
       fcntl(uart_fd, F_SETFL, O_ASYNC);
       printf("  ACK wait... \n");

       int timeout = 30000;
       while(timeout--){

        	if(feedback == 1){
        		printf("  ACK 수신 \n");
        		return 1;
        	}
        	else {
        		printf("  NACK → 재전송\n");
        		break;
        	}
        }

        printf("  타임아웃 → retry %d/%d\n", retry+1, MAX_RETRY);
    }
    return 0;
}

/* READY 수신 */
int wait_ready(void)
{
    printf("[1] MCU READY 대기 중...\n");

    uint8_t buf[16] = {0};
    int total = 0;
    int timeout = 50;

    while(timeout--) {
        int n = read(uart_fd, &buf[total], 1);
        if(n > 0) {
            total += n;
            if(strchr((char*)buf, 'R')) {
                printf("    READY 확인 ✅\n");
                tcflush(uart_fd, TCIFLUSH);
                return 1;
            }
        }
        usleep(100000);
    }
    printf("    READY 타임아웃 ✗\n");
    return 0;
}

/* bl_sender.c main을 이걸로 교체 */
int main(int argc, char *argv[])
{
    if(argc < 3) {
        printf("사용법: %s <bin> <port>\n", argv[0]);
        return 1;
    }

    const char *bin_path  = argv[1];
    const char *uart_port = argv[2];

    /*  bin 파일 열기  */
    printf("[0] bin 파일 열기: %s\n", bin_path);
    int bin_fd = open(bin_path, O_RDONLY);
    if(bin_fd < 0) { perror("bin open fail"); return 1; }

    /*  파일 크기 확인  */
    struct stat st;
    fstat(bin_fd, &st);
    int file_size  = st.st_size;
    int page_count = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    printf("    파일 크기: %d bytes\n", file_size);
    printf("    크기: %d bytes / 페이지: %d\n", file_size, page_count);

    /*   UART 열기   */
    uart_fd = uart_open(uart_port);
    if(uart_fd < 0) return 1;
    /*
    /* ── READY 대기 ── */
    if(!wait_ready()) {
        close(bin_fd);
        close(uart_fd);
        return 1;
    }*/

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

    if(send_packet(&pkt))
        printf("    완료! MCU App 점프\n");
    else
        printf("    END ACK 없음 ✗\n");

    close(bin_fd);
    close(uart_fd);
    return 0;

}
