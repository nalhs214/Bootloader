#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/types.h>

int main(void)
{
	/* opne port */
	int fd = open("/dev/ttyUSB0", O_RDWR);
	if(fd<0) { perror("open fail"); return 1; }

	/* port setting */ 
	struct termios tty;
	tcgetattr(fd, &tty);
	cfmakeraw(&tty);
	cfsetispeed(&tty, B9600);
	cfsetospeed(&tty, B9600);
	/*
	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CSIZE;
	tty.c_cflag |=  CS8;
	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;
	*/
	tty.c_cc[VTIME] = 10;
	tty.c_cc[VMIN] = 0;
	tcsetattr(fd, TCSANOW, &tty);

	/* raw mode change */
	struct termios stdin_org;
	tcgetattr(0, &stdin_org);
	struct termios stdin_raw = stdin_org;
	cfmakeraw(&stdin_raw);
	tcsetattr(0, TCSANOW, &stdin_raw);
	
	fd_set fds;
	char buf[64];

	printf("push key  or  Ctrl+C : \r\n");

	while(1)
	{
		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(fd, &fds);
		select(fd + 1, &fds, NULL, NULL, NULL);

        	/* 키보드 입력 */
	        if(FD_ISSET(0, &fds)) {
			int n = read(0, buf, 64);
			if(buf[0] == 3) break;
	        	write(fd, buf, n);         // MCU로 전송
	        	printf("send : %.*s\r\n", n, buf);
			fflush(stdout);
	        }

        	/* MCU 수신 */
	        if(FD_ISSET(fd, &fds)) {
	        	int n = read(fd, buf, 64);
			if(n > 0){
				for(int i = 0; i < n; i++){
					if(buf[i] != '\r' && buf[i] != '\n'){
						printf("%c", buf[i]);
					}
				}
				printf("\r\n");
				fflush(stdout);
			}
		}
	}

	tcsetattr(0, TCSANOW, &stdin_org);
	printf("\r\nfinish\r\n");
	close(fd);
	return 0;
}
