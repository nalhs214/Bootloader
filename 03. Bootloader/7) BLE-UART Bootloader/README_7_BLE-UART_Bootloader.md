# 7) BLE-UART Bootloader

프로젝트의 **최종 결과물**이다. 프로젝트 5의 UART 부트로더를 BLE 모듈 NU-HC(nRF54L05) 위에서 동작하도록 발전시켜, **하드웨어 디버거 없이 BLE로 펌웨어를 무선 갱신**한다. BLE 환경의 제약(물리 리셋 불가, flush 단위 전송, 노이즈)에 대응하기 위해 인터럽트 큐, `'S'/'R'` 핸드셰이크, START_CODE 재동기를 도입했다.

> 환경: ATmega1281 (16 MHz) · UART 19200 8N1 · NU-HC(nRF54L05) ×2 · Ubuntu(VMware) `bl_sender`

---

## 목차

1. [아키텍처](#1-아키텍처)
2. [프로젝트 5 → 7 변경점](#2-프로젝트-5--7-변경점)
3. [이중 UART 구조](#3-이중-uart-구조)
4. [인터럽트 + 원형큐](#4-인터럽트--원형큐)
5. [핸드셰이크 (S/R)](#5-핸드셰이크-sr)
6. [START_CODE 재동기](#6-start_code-재동기)
7. [메인 흐름](#7-메인-흐름)
8. [응용 점프](#8-응용-점프)
9. [사전 조건](#9-사전-조건)
10. [리소스 / 파일](#10-리소스--파일)

---

## 1. 아키텍처

```text
┌──────────────┐ USB  ┌──────────┐ BLE  ┌──────────┐ UART1 ┌──────────────┐
│ Linux Host   │─────▶│ NU-HC #1 │═════▶│ NU-HC #2 │──────▶│ ATmega1281   │
│ bl_sender    │◀─────│ Central  │◀═════│Peripheral│◀──────│ Bootloader   │
└──────────────┘19200 └──────────┘ NUS  └──────────┘ 19200 └──────┬───────┘
                       Throughput        Throughput               │ UART0
                                         182B/16ms flush          ▼ (디버그)
                                                            PC 터미널
```

| 계층 | 책임 |
| --- | --- |
| Host | bin 분할, 262B 패킷, 20B 청크 송신, 재전송, 비동기 ACK 수신 |
| NU-HC ×2 | UART ↔ BLE 투명 전송 (내용 무해석) |
| MCU | 핸드셰이크, 재동기, CRC 검증, SPM 기록, 응답 |

---

## 2. 프로젝트 5 → 7 변경점

| 항목 | 5) UART 직결 | 7) BLE |
| --- | --- | --- |
| 전송 매체 | UART 직결 | BLE (NU-HC ×2) |
| UART 채널 | UART0 단일 | UART0(디버그) + UART1(NU-HC) |
| 수신 방식 | 폴링 | **인터럽트 + 256B 원형큐** |
| 송신 방식 | 폴링 | 인터럽트 (UDRIE 큐) |
| 진입 동기화 | 아무 byte 도착 | **'S'/'R' 핸드셰이크** |
| 정렬 복구 | START 1회 확인 | **START_CODE 재동기 루프** |
| 큐 관리 | — | `USART1_RxFlush`, `USART1_FlushTx` |

이유: BLE 경로는 (1) DTR 물리 리셋이 안 건너가고, (2) 182B/16ms 단위로 flush되며, (3) 핸드셰이크 잔재·노이즈가 낄 수 있다. 이를 흡수하기 위한 구조다.

---

## 3. 이중 UART 구조

```text
UART0 (PE0/PE1) ── 디버그 출력 전용 (HEX 덤프, 진행 표시)
UART1 (PD2/PD3) ── NU-HC 데이터 경로 (패킷 수신, ACK/NAK 송신)
```

채널을 분리해, 부트로더 데이터(UART1)와 디버그 로그(UART0)가 섞이지 않게 한다. 각 채널은 독립된 RX/TX 원형큐를 가진다.

```c
static volatile uint8_t rx0_buf[256], tx0_buf[256];   // UART0
static volatile uint8_t rx1_buf[256], tx1_buf[256];   // UART1
```

---

## 4. 인터럽트 + 원형큐

폴링의 오버런 문제를 해결한다. RX는 항상 켜고, TX는 보낼 때만 켠다.

```c
/* RX: 수신 즉시 큐에 적재 */
ISR(USART1_RX_vect) {
    uint8_t c = UDR1;
    uint8_t next = (rx1_head + 1) & BUF_MASK;
    if (next != rx1_tail) { rx1_buf[rx1_head] = c; rx1_head = next; }
}

/* TX: 큐에서 송신, 비면 인터럽트 끔 */
ISR(USART1_UDRE_vect) {
    if (tx1_head == tx1_tail) UCSR1B &= ~(1 << UDRIE1);   // 큐 빔 → off
    else { UDR1 = tx1_buf[tx1_tail]; tx1_tail = (tx1_tail+1) & BUF_MASK; }
}

void USART1_Transmit(unsigned char data) {
    uint8_t next = (tx1_head + 1) & BUF_MASK;
    while (next == tx1_tail);                  // 큐 가득 차면 대기
    tx1_buf[tx1_head] = data; tx1_head = next;
    UCSR1B |= (1 << UDRIE1);                    // 송신 인터럽트 켬
}
```

| 특성 | 동작 |
| --- | --- |
| 큐 크기 | 256B, 인덱스 `uint8_t` (AVR 원자적 접근) |
| RX 인터럽트 | 항상 활성 (`RXCIE`) |
| TX 인터럽트 | 보낼 때 set, 큐 비면 ISR이 clear (무한 호출 방지) |

### 보조 함수

```c
void USART1_RxFlush(void) {           // 핸드셰이크 직후 1회: 잔재 제거
    uint8_t sreg = SREG; cli();
    rx1_tail = rx1_head;              // 큐 비우기
    SREG = sreg;                      // 인터럽트 상태 복원
}

void USART1_FlushTx(void) {           // 송신 완료 보장
    while (tx1_head != tx1_tail);     // 큐 빌 때까지
    while (!(UCSR1A & (1 << TXC1)));   // 마지막 비트까지
}
```

---

## 5. 핸드셰이크 (S/R)

DTR 물리 리셋이 BLE를 통과하지 못하므로, 인밴드 신호로 시작을 맞춘다.

```text
Host                              MCU (부트 대기 중)
 │  'S' 반복 (200ms)  ──────────▶ │  'S' 수신
 │                                │  USART1_RxFlush()   ← 잔재 제거
 │ ◀──────────────────  'R'       │  USART1_Transmit('R')
 │  'R' 수신 → 전송 시작           │  USART1_FlushTx() → run_bootloader
```

```c
if (c == 'S') {
    USART1_RxFlush();              // RX 큐의 핸드셰이크 잔재 제거
    USART1_Transmit('R');          // 준비 응답
    USART1_FlushTx();              // 'R' 전송 완료 대기
    run_bootloader();
}
```

---

## 6. START_CODE 재동기

핸드셰이크 잔재나 노이즈가 패킷 앞에 끼어도 정렬을 복구하는 안전망이다.

```c
uint8_t recv_packet(bl_data_packet_t *pkt) {
    uint8_t raw[PKT_SIZE];

    do {
        raw[0] = USART1_Receive();
    } while (raw[0] != START_CODE);    // 0xEF 찾을 때까지 폐기

    for (uint16_t i = 1; i < PKT_SIZE; i++)
        raw[i] = USART1_Receive();     // 나머지 261B

    /* 파싱 + CRC16 검증 → msg_type 또는 NAK */
}
```

정상 경로에서는 첫 byte가 곧 0xEF라 폐기 없이 통과한다. 잔재가 있을 때만 재동기 동작을 한다.

---

## 7. 메인 흐름

```c
int main(void) {
    DDRC = 0xFF; PORTC = 0x00;
    UART0_init(); UART1_init();
    MCUCR = (1 << IVCE); MCUCR = (1 << IVSEL);   // 벡터 → 부트 영역
    sei();

    uint16_t timeout = 10000;
    while (timeout--) {                 // 약 100초 부트 대기
        PORTC = 0x05; _delay_ms(10);
        if (USART1_Available()) {
            uint8_t c = USART1_Receive();
            if (c == 'S') {             // 핸드셰이크
                USART1_RxFlush();
                USART1_Transmit('R');
                USART1_FlushTx();
                run_bootloader();
            }
        }
    }
}
```

```c
void run_bootloader(void) {
    uint32_t write_addr = 0x0000;
    while (1) {
        uint8_t result = recv_packet(&pkt);
        _delay_ms(100);
        if (result == MSG_DATA) {
            if (write_addr >= BOOT_START) { USART1_Transmit(NAK); break; }  // 영역 보호
            flash_page_write(write_addr, pkt.data);
            USART1_Transmit(ACK); write_addr += PAGE_SIZE; PORTC ^= 0x01;
        }
        else if (result == MSG_END) { USART1_Transmit(ACK); _delay_ms(100); jump_to_app(); }
        else USART1_Transmit(NAK);
    }
}
```

> `_delay_ms(100)`은 수신 직후 응답 송신 안정화용. `USART1_FlushTx` 기반으로 대체 가능(개선 항목).

---

## 8. 응용 점프

```c
void jump_to_app(void) {
    cli();
    MCUCR = (1 << IVCE);
    MCUCR = (0 << IVSEL);        // 벡터 → 응용 영역 복원
    UCSR0B = 0x00;               // UART 비활성화
    asm volatile("jmp 0x0");
}
```

부트 진입 시 set한 `IVSEL`을 0으로 되돌려, 응용 프로그램이 자신의 인터럽트 벡터를 쓰게 한다.

---

## 9. 사전 조건

| 항목 | 내용 |
| --- | --- |
| BLE 연결 | NU-HC #1 ↔ #2 연결 + Throughput Mode (`+GATTSTAT=0,3`) |
| 배선 | NU-HC #2 TX → MCU PD2(RXD1), GND 공통 |
| 포트 | `/dev/ttyUSB0`을 `bl_sender`만 점유 (터미널 종료) |
| 리셋 | 'S' 송신 중(약 100초 내) MCU 수동 리셋 → 부트 대기 진입 |

> NU-HC가 미연결(Command Mode)이면 raw 패킷을 AT로 해석해 `ERROR`를 반송하므로, 반드시 연결 + Throughput 상태에서 전송한다.

---

## 10. 리소스 / 파일

| 항목 | 값 |
| --- | --- |
| 부트로더 코드 | 약 1638 byte (.text) |
| RAM 사용 | 약 1032 byte (.bss, 원형큐 4×256 포함) |
| 부트 영역 | 0x1E000 |
| 시험 바이너리 | `LED_blink.bin` 1050 byte = 5 페이지 |

| 파일 | 설명 |
| --- | --- |
| `main.c` | 부트 대기·핸드셰이크·`run_bootloader` |
| `protocol.c/.h` | `crc16`, `recv_packet` (재동기·검증) |
| `boot.c/.h` | `flash_page_write`, `jump_to_app` |
| `uart.c/.h` | 인터럽트 UART (RX/TX 큐, RxFlush, FlushTx) |

호스트 송신 도구는 프로젝트 5의 `bl_sender`(20B 청크 송신 버전)를 사용한다. 상세 명세·트러블슈팅은 `00.docs` 폴더 참고.
