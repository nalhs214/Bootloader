# Linux ↔ MCU UART 시리얼 통신 가이드

> 작성일: 2026-06-02  
> 환경: Ubuntu (VMware), ATmega1281, CP210x USB-UART

---

## 목차

1. [환경 설정](#1-환경-설정)
2. [MCU → Linux 수신](#2-mcu--linux-수신)
3. [Linux ↔ MCU echo (select 방식)](#3-linux--mcu-echo-select-방식)
4. [Linux ↔ MCU echo (signal 방식)](#4-linux--mcu-echo-signal-방식)
5. [핵심 개념 정리](#5-핵심-개념-정리)
6. [트러블슈팅](#6-트러블슈팅)

---

## 1. 환경 설정

### VMware CP210x 연결

```
VMware 상단 메뉴
→ VM → Removable Devices
→ Silicon Labs CP210x
→ Connect 클릭
```

### 포트 확인

```bash
ls /dev/ttyUSB*
# 결과: /dev/ttyUSB0
```

### 권한 설정

```bash
sudo chmod 666 /dev/ttyUSB0
```

### 필요 헤더

```c
#include <stdio.h>
#include <fcntl.h>      // open()
#include <unistd.h>     // read(), write(), close()
#include <termios.h>    // 시리얼 포트 설정
#include <string.h>     // memset
#include <signal.h>     // signal (signal 방식)
#include <sys/select.h> // select(), fd_set (select 방식)
#include <sys/types.h>
```

### 포트 열기 및 설정 (공통)

```c
/* 포트 열기 */
int fd = open("/dev/ttyUSB0", O_RDWR);
if(fd < 0) { perror("open fail"); return 1; }

/* 포트 설정 */
struct termios tty;
tcgetattr(fd, &tty);

cfmakeraw(&tty);            // raw 모드 (CR/LF 변환 없음)
cfsetispeed(&tty, B9600);   // 수신 9600baud
cfsetospeed(&tty, B9600);   // 송신 9600baud
tty.c_cc[VTIME] = 10;       // 1초 타임아웃
tty.c_cc[VMIN]  = 0;
tcsetattr(fd, TCSANOW, &tty); // 즉시 적용
```

### cfmakeraw() 역할

```
CR/LF 변환 없음      → 바이너리 데이터 그대로
echo 끄기            → 수신 데이터 자동 출력 안 함
ICANON 해제          → 줄 단위 처리 안 함
특수문자 처리 없음   → Ctrl+C 등 그대로 전달
```

---

## 2. MCU → Linux 수신

### MCU 코드 (ATmega1281)

```c
#include <avr/io.h>
#include <stdio.h>
#include <util/delay.h>
#include "uart.h"

int main(void)
{
    UART_init();
    UART_stdout_init();   // printf → UART 연결

    while(1)
    {
        printf("hello\r\n");
        _delay_ms(1000);
    }
}
```

> ⚠️ `UART_init()`과 `UART_stdout_init()` 반드시 호출

### Linux 수신 코드

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

int main(void)
{
    int fd = open("/dev/ttyUSB0", O_RDONLY);
    if(fd < 0) { perror("open fail"); return 1; }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN]  = 0;
    tcsetattr(fd, TCSANOW, &tty);

    char buf[64];
    int n;

    while((n = read(fd, buf, 64)) > 0) {
        /* \r\n 제거 후 출력 */
        for(int i = 0; i < n; i++) {
            if(buf[i] != '\r' && buf[i] != '\n')
                printf("%c", buf[i]);
        }
        printf("\n");
        fflush(stdout);
    }

    close(fd);
    return 0;
}
```

### 빠른 확인 (cat 사용)

```bash
stty -F /dev/ttyUSB0 9600 raw -echo
cat /dev/ttyUSB0
# MCU에서 보내는 데이터 바로 확인
# 종료: Ctrl+C
```

---

## 3. Linux ↔ MCU echo (select 방식)

### 동작 원리

```
fd_set으로 stdin(0)과 ttyUSB0(fd) 동시 감시
select()가 데이터 오면 깨어남
FD_ISSET으로 어디서 왔는지 구분
```

### MCU 코드

```c
int main(void)
{
    UART_init();

    while(1)
    {
        unsigned char c = USART_Receive();
        USART_Transmit(c);   // 받은 문자 그대로 echo
    }
}
```

### Linux 코드 (MCU_serial_select.c)

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>

int main(void)
{
    /* 포트 열기 */
    int fd = open("/dev/ttyUSB0", O_RDWR);
    if(fd < 0) { perror("open fail"); return 1; }

    /* 포트 설정 */
    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN]  = 0;
    tcsetattr(fd, TCSANOW, &tty);

    /* stdin raw 모드 → 한 글자씩 즉시 전송 */
    struct termios stdin_org;
    tcgetattr(0, &stdin_org);
    struct termios stdin_raw = stdin_org;
    cfmakeraw(&stdin_raw);
    tcsetattr(0, TCSANOW, &stdin_raw);

    fd_set fds;
    char buf[64];

    printf("push key or Ctrl+C:\r\n");

    while(1)
    {
        FD_ZERO(&fds);
        FD_SET(0,  &fds);   // stdin 등록
        FD_SET(fd, &fds);   // ttyUSB0 등록

        select(fd+1, &fds, NULL, NULL, NULL);

        /* 키보드 → MCU */
        if(FD_ISSET(0, &fds)) {
            int n = read(0, buf, 64);
            if(buf[0] == 3) break;  // Ctrl+C 종료
            write(fd, buf, n);
            printf("send: %.*s\r\n", n, buf);
            fflush(stdout);
        }

        /* MCU → 화면 */
        if(FD_ISSET(fd, &fds)) {
            int n = read(fd, buf, 64);
            if(n > 0) {
                for(int i = 0; i < n; i++) {
                    if(buf[i] != '\r' && buf[i] != '\n')
                        printf("%c", buf[i]);
                }
                printf("\r\n");
                fflush(stdout);
            }
        }
    }

    /* stdin 복구 */
    tcsetattr(0, TCSANOW, &stdin_org);
    printf("\r\nfinish\r\n");
    close(fd);
    return 0;
}
```

### select() 핵심 개념

```
fd_set   = 감시할 fd 목록 (비트 배열)
FD_ZERO  = fd_set 초기화
FD_SET   = fd 등록
FD_ISSET = 데이터 왔는지 확인

select(fd+1, &fds, NULL, NULL, NULL)
  → 등록된 fd 중 데이터 오면 리턴
  → 없으면 대기
  → 첫 인자 = 감시할 fd 최대값 + 1
```

---

## 4. Linux ↔ MCU echo (signal 방식)

### 동작 원리

```
SIGIO 신호로 비동기 처리
MCU 데이터 오면 자동으로 핸들러 호출
메인 루프는 키보드 입력만 처리
```

### MCU 코드 (select 방식과 동일)

```c
int main(void)
{
    UART_init();

    while(1)
    {
        unsigned char c = USART_Receive();
        USART_Transmit(c);
    }
}
```

### Linux 코드 (MCU_serial_signal.c)

```c
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <signal.h>

int fd;  // 전역 변수 (핸들러에서 접근)

/* MCU 데이터 오면 자동 호출 */
void sigio_handler(int sig)
{
    char buf[64];
    int n = read(fd, buf, 64);
    if(n > 0) {
        for(int i = 0; i < n; i++) {
            if(buf[i] != '\r' && buf[i] != '\n')
                printf("%c", buf[i]);
        }
        printf("\r\n");
        fflush(stdout);
    }
}

int main(void)
{
    /* 포트 열기 */
    fd = open("/dev/ttyUSB0", O_RDWR);  // 전역 fd 사용
    if(fd < 0) { perror("open fail"); return 1; }

    /* 포트 설정 */
    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN]  = 0;
    tcsetattr(fd, TCSANOW, &tty);

    /* SIGIO 설정 3단계 */
    signal(SIGIO, sigio_handler);   // 핸들러 등록
    fcntl(fd, F_SETOWN, getpid());  // 신호 수신 프로세스 지정
    fcntl(fd, F_SETFL, O_ASYNC);    // 비동기 모드 활성화

    /* stdin raw 모드 */
    struct termios stdin_org;
    tcgetattr(0, &stdin_org);
    struct termios stdin_raw = stdin_org;
    cfmakeraw(&stdin_raw);
    tcsetattr(0, TCSANOW, &stdin_raw);

    char buf[64];
    printf("push key or Ctrl+C:\r\n");

    /* 메인 루프 - 키보드만 처리 */
    while(1)
    {
        int n = read(0, buf, 64);
        if(buf[0] == 3) break;
        write(fd, buf, n);
        printf("send: %.*s\r\n", n, buf);
        fflush(stdout);
    }

    tcsetattr(0, TCSANOW, &stdin_org);
    printf("\r\nfinish\r\n");
    close(fd);
    return 0;
}
```

### SIGIO 설정 상세

```c
signal(SIGIO, sigio_handler);
/* "SIGIO 신호 오면 sigio_handler 호출해라" */

fcntl(fd, F_SETOWN, getpid());
/* "ttyUSB0 신호를 현재 프로세스로 전달해라" */

fcntl(fd, F_SETFL, O_ASYNC);
/* "ttyUSB0에 데이터 오면 SIGIO 자동 발생해라" */
```

---

## 5. 핵심 개념 정리

### select vs signal 비교

| 항목 | select | signal |
|------|--------|--------|
| 방식 | 동기 | 비동기 |
| 감시 | 루프마다 select() | 자동 핸들러 호출 |
| 구조 | 단일 루프 | 메인 + 핸들러 분리 |
| 복잡도 | 단순 | 약간 복잡 |
| 부트로더 적합성 | ✅ | ✅ |

### stdin raw 모드

```
기본 (cooked):  엔터 눌러야 전달
raw 모드:       한 글자 누르면 즉시 전달

설정:
  cfmakeraw(&stdin_raw)
  tcsetattr(0, TCSANOW, &stdin_raw)

복구 (종료 시 필수!):
  tcsetattr(0, TCSANOW, &stdin_org)
```

### \r\n 필터링

```c
/* MCU가 \r\n 포함해서 echo
   Linux에서 제거 후 출력 */
if(buf[i] != '\r' && buf[i] != '\n')
    printf("%c", buf[i]);
```

### fd 전역 변수 주의사항

```c
int fd;          // 전역 선언

int main(void) {
    fd = open(...);   // ✅ 전역 fd 사용
    // int fd = open(...);  ✗ 지역 변수로 재선언하면
                            //   핸들러에서 전역 fd=0 사용
                            //   → stdin 읽어버림
}
```

---

## 6. 트러블슈팅

### open fail: No such file or directory

```
원인: CP210x Ubuntu에 연결 안 됨
해결:
  VMware → VM → Removable Devices
  → CP210x → Connect

또는 포트 이름 확인:
  ls /dev/tty*
  → ttyUSB1 또는 ttyACM0 일 수 있음
```

### recv 출력 안 됨 (select 방식)

```
원인 1: MCU echo 코드 없음
  → USART_Receive() + USART_Transmit() 확인

원인 2: \r\n 필터링 문제
  → MCU가 \r\n 없이 보내면
     누적 버퍼 방식 사용 또는
     즉시 출력 방식으로 변경
```

### recv 출력 안 됨 (signal 방식)

```
원인: fd 전역/지역 변수 충돌
  int fd;           // 전역
  int fd = open();  // 지역 → 핸들러가 전역 fd(=0) 사용

해결:
  main에서 int 제거
  fd = open("/dev/ttyUSB0", O_RDWR);
```

### 터미널이 이상하게 동작

```
원인: stdin raw 모드 복구 안 됨
해결:
  reset 명령어 입력
  또는 터미널 재시작
```

---

## 컴파일 및 실행

```bash
# select 방식
gcc -o Msel MCU_serial_select.c
./Msel

# signal 방식
gcc -o Msig MCU_serial_signal.c
./Msig

# 종료: Ctrl+C (ASCII 3)
```
