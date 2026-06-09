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
#include "packet.h"
#include "uart.h"


volatile uint8_t got_data = 0;


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
    printf("bin 파일 열기: %s\n", bin_path);
    int bin_fd = open(bin_path, O_RDONLY);
    if(bin_fd < 0) { perror("bin open fail"); return 1; }

    /*  파일 크기 확인  */
    struct stat st;
    fstat(bin_fd, &st);
    int file_size  = st.st_size;
    int page_count = (file_size + PAGE_SIZE - 1) / PAGE_SIZE;
    printf("    파일 크기: %d bytes\n", file_size);
    printf("    크기: %d bytes / 페이지: %d\n", file_size, page_count);

    /*   UART 열기   +  DTR열기   */
    uart_fd = uart_open(uart_port);
    if(uart_fd < 0) { close(bin_fd); return 1;}

    /* ── READY 대기 ── */
    if(!wait_ready()) { close(bin_fd); close(uart_fd); return 1; }

    int ret = upload(bin_fd, page_count);

    close(bin_fd);
    close(uart_fd);
    return 0;

}
