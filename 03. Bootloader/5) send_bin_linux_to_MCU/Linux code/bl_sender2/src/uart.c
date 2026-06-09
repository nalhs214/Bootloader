/*
 * uart.c
 *
 * sigio_handler :
 * uart_open
 * wait_ready
 *
 */
#include "uart.h"
#include "packet.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>

/* ── 전역 변수 정의 ── */
int uart_fd;
volatile int8_t  feedback   = -1;  // -1=미수신 0=NAK 1=ACK
volatile uint8_t ready_recv =  0;  // READY 수신 플래그


/* ── SIGIO 핸들러 ── */
/*
 * MCU가 보내는 신호 처리:
 *   'R' (0x52) → READY
 *   0x06       → ACK
 *   0x15       → NAK
 */
void sigio_handler(int sig)
{
    uint8_t resp = 0;
    int n = read(uart_fd, &resp, 1);
    if(n == 1) {
        printf("  [SIGIO] recv: 0x%02X\n", resp);

        if(resp == 'R') {
            ready_recv = 1;          // READY 감지
            printf("  [SIGIO] READY!\n");
        }
        else if(resp == ACK) {
            feedback = 1;            // ACK
            printf("  [SIGIO] ACK\n");
        }
        else if(resp == NAK) {
            feedback = 0;            // NAK
            printf("  [SIGIO] NAK\n");
        }
        else {
            printf("  [SIGIO] unknown: 0x%02X\n", resp);
        }
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

void DTR_Reset(void)
{
    /* DTR로 MCU 자동 리셋 */
    int dtr = TIOCM_DTR;
    ioctl(uart_fd, TIOCMBIC, &dtr);   // DTR LOW → 리셋
    usleep(100000);               // 100ms
    ioctl(uart_fd, TIOCMBIS, &dtr);   // DTR HIGH → 리셋 해제
    printf("  DTR Reset success\n");
}


/* READY 수신 */
int wait_ready(void)
{

    printf("[1] MCU Booting 대기 중...\n");
    int timeout = 50;
    while(timeout--) {
        if(ready_recv) {
            printf("    READY 확인 \n");
            ready_recv = 0;			//ready reset

            /* READY 후 MCU 준비 대기 */
            usleep(500000);  // 500ms ← 추가
        }
        usleep(100000);
    }
    printf("    Booting 타임아웃 ✗\n");

    /* DTR Reset */
    printf("  DTR Reset\n");
    DTR_Reset();


    printf("[1_1] MCU Reset 대기 중...\n");
    timeout = 50;
    while(timeout--) {
        if(ready_recv) {
            printf("    READY 확인 \n");
            ready_recv = 0;			//ready reset

            /* READY 후 MCU 준비 대기 */
            usleep(500000);  // 500ms ← 추가
            return 1;
        }
        usleep(100000);
    }
    printf("    Booting 타임아웃 ✗\n");
    return 0;
}
