# 6) Linux_test

부트로더의 **Linux 호스트 측 기능을 개별적으로 검증한 참조 코드 모음**이다. 파일 입출력, 시리얼 통신(select / signal)을 독립 예제로 구현하여, 이후 송신 도구(`bl_sender`)의 빌딩블록으로 활용한다.

> 환경: Ubuntu(VMware) · gcc · CP210x USB-UART (`/dev/ttyUSB0`)
> 관련: 이 코드들의 상세 가이드는 `4) MCU to linux/Linux_MCU_Serial_README.md` 참고

---

## 목차

1. [구성](#1-구성)
2. [가. 파일 입출력](#2-가-파일-입출력)
3. [나. select 방식 에코](#3-나-select-방식-에코)
4. [다. signal 방식 에코](#4-다-signal-방식-에코)
5. [select vs signal](#5-select-vs-signal)
6. [빌드·실행](#6-빌드실행)
7. [부트로더로의 연결](#7-부트로더로의-연결)

---

## 1. 구성

| 파일 | 역할 | 부트로더에서의 용도 |
| --- | --- | --- |
| `가. File_read_write.c` | open/write/read 기본 | bin 파일을 페이지 단위로 읽기 |
| `나. MCU_serial_select.c` | `select()` 기반 시리얼 에코 | 동기 수신 방식 검증 |
| `다. MCU_serial_signal.c` | `signal(SIGIO)` 기반 비동기 에코 | 비동기 ACK 수신 방식 검증 |

```text
File I/O ──┐
           ├─→ bl_sender 의 bin 읽기 + 비동기 응답 수신
select  ──┤
signal  ──┘
```

---

## 2. 가. 파일 입출력

bin 파일을 읽어 페이지 단위로 다루기 위한 기본 동작이다.

```c
#include <fcntl.h>
#include <unistd.h>

int main(void) {
    /* 쓰기 */
    int fd = open("test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    write(fd, "Hello World!\n", 13);
    close(fd);

    /* 읽기 */
    fd = open("test.txt", O_RDONLY);
    char buf[64] = {0};
    int n = read(fd, buf, 64);          // n = 읽은 byte 수
    close(fd);

    printf("읽은 내용: %s", buf);
    printf("읽은 바이트: %d\n", n);
}
```

핵심: `read`의 반환값(읽은 byte 수)으로 파일 끝·페이지 경계를 판단한다. `bl_sender`의 `upload`가 256B씩 `read`하여 페이지를 구성하는 기반이다.

---

## 3. 나. select 방식 에코

`select()`로 **stdin과 시리얼 포트를 동시 감시**하여, 키보드 입력은 MCU로 보내고 MCU 응답은 화면에 출력한다.

```c
#include <sys/select.h>

int fd = open("/dev/ttyUSB0", O_RDWR);
/* termios: cfmakeraw, B9600, VTIME=10, VMIN=0 */

fd_set fds;
while (1) {
    FD_ZERO(&fds);
    FD_SET(0,  &fds);          // stdin 등록
    FD_SET(fd, &fds);          // 시리얼 등록
    select(fd + 1, &fds, NULL, NULL, NULL);   // 둘 중 데이터 오면 깨어남

    if (FD_ISSET(0, &fds))  { /* 키보드 → MCU write */ }
    if (FD_ISSET(fd, &fds)) { /* MCU → 화면 출력 */ }
}
```

| 함수 | 역할 |
| --- | --- |
| `FD_ZERO/FD_SET` | 감시할 fd 집합 구성 |
| `select(maxfd+1, ...)` | 등록된 fd 중 데이터 오면 반환 |
| `FD_ISSET` | 어느 fd에 데이터가 왔는지 확인 |

동기 방식 — 매 루프에서 `select`로 명시적으로 감시한다.

---

## 4. 다. signal 방식 에코

`SIGIO` 신호로 **시리얼 수신을 비동기 처리**한다. 데이터가 오면 핸들러가 자동 호출되고, 메인 루프는 키보드만 처리한다.

```c
#include <signal.h>

int fd;   // 전역 (핸들러 접근)

void sigio_handler(int sig) {            // 데이터 도착 시 자동 호출
    char buf[64];
    int n = read(fd, buf, 64);
    /* 화면 출력 */
}

int main(void) {
    fd = open("/dev/ttyUSB0", O_RDWR);   // 전역 fd에 대입 (재선언 금지!)
    /* termios 설정 */

    signal(SIGIO, sigio_handler);        // ① 핸들러 등록
    fcntl(fd, F_SETOWN, getpid());       // ② 신호 수신 프로세스 지정
    fcntl(fd, F_SETFL, O_ASYNC);         // ③ 비동기 모드 활성화

    while (1) { /* 키보드만 처리 */ }
}
```

| 단계 | 의미 |
| --- | --- |
| `signal(SIGIO, ...)` | SIGIO 발생 시 핸들러 호출 |
| `F_SETOWN, getpid()` | 이 포트의 신호를 현재 프로세스로 |
| `F_SETFL, O_ASYNC` | 데이터 도착 시 SIGIO 자동 발생 |

> 주의: `fd`를 main에서 `int fd = open(...)`로 재선언하면 전역 fd가 0(stdin)인 채로 핸들러가 stdin을 읽는 버그가 생긴다. 반드시 전역 `fd`에 대입한다.

---

## 5. select vs signal

| 항목 | select | signal |
| --- | --- | --- |
| 방식 | 동기 | 비동기 |
| 감시 | 루프마다 `select()` | 데이터 도착 시 핸들러 자동 호출 |
| 구조 | 단일 루프 | 메인 + 핸들러 분리 |
| 부트로더 채택 | — | ✅ `bl_sender`가 채택 |

`bl_sender`는 **signal(SIGIO) 방식**을 사용한다. 송신 중에도 ACK/NAK가 도착하면 핸들러가 즉시 `feedback`을 갱신하므로, 메인 흐름은 전송에 집중할 수 있다.

---

## 6. 빌드·실행

```bash
gcc -o Msel "나. MCU_serial_select.c" && ./Msel    # select 방식
gcc -o Msig "다. MCU_serial_signal.c" && ./Msig    # signal 방식
# 종료: Ctrl+C (ASCII 3)
```

사전: VMware에서 CP210x 연결, `sudo chmod 666 /dev/ttyUSB0`, MCU에 에코 코드(`USART_Receive`→`USART_Transmit`) 적재.

---

## 7. 부트로더로의 연결

| 이 프로젝트 | 부트로더 적용 |
| --- | --- |
| File I/O | `upload`의 256B 페이지 읽기 |
| signal(SIGIO) | `bl_sender`의 비동기 ACK/NAK 수신 (`sigio_handler`) |
| termios raw | `uart_open`의 포트 설정 |

여기서 검증한 패턴은 프로젝트 4(에코)·5(UART 부트로더)·7(BLE 부트로더)의 호스트 코드에 통합된다.
