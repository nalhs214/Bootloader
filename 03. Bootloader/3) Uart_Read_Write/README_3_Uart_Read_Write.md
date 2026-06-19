# 3) Uart_Read_Write

**UART로 수신한 데이터를 플래시에 기록하는 첫 부트로더**다. 프로젝트 2의 플래시 기록(`flash_page_write`)과 UART 통신을 결합하여, 호스트가 보낸 펌웨어를 페이지 단위로 받아 기록하고 응용 영역으로 점프하는 완결된 동작을 구현한다.

> 환경: ATmega1281 (16 MHz) · UART 9600 8N1 (폴링) · 페이지 256 byte
> 입력: Python으로 hex → bin 변환한 펌웨어 바이너리

---

## 목차

1. [아키텍처](#1-아키텍처)
2. [동작 흐름](#2-동작-흐름)
3. [페이지 수신 로직](#3-페이지-수신-로직)
4. [메인 루프](#4-메인-루프)
5. [UART 드라이버](#5-uart-드라이버)
6. [응용 영역 점프](#6-응용-영역-점프)
7. [한계와 다음 단계](#7-한계와-다음-단계)
8. [파일 구성](#8-파일-구성)

---

## 1. 아키텍처

```text
Host (PC)  ──UART 9600──  ATmega1281 Bootloader
  bin 송신                  ┌─────────────────────┐
  256B씩                    │ recv_page (폴링 수신) │
                            │   ↓                  │
                            │ flash_page_write     │  프로젝트 2 재사용
                            │   ↓                  │
                            │ ACK 송신             │
                            │   ↓ (END_SIGNAL)     │
                            │ jump_to_app          │
                            └─────────────────────┘
```

이 단계는 패킷 헤더·CRC 없이 **raw 256 byte 페이지**를 그대로 받는다. 종료는 첫 byte가 `0xFF`(END_SIGNAL)인지로 판단한다.

---

## 2. 동작 흐름

```text
1. UART 초기화, PORTC LED 3회 점멸 (시작 표시)
2. "READY\r\n" 송신 → 호스트에 준비 완료 통지
3. 반복:
     recv_page(buf):
       첫 byte == 0xFF  → 종료(false), ACK 후 break
       아니면           → 나머지 255B 수신(true)
     flash_page_write(addr, buf)  → 페이지 기록
     ACK(0x06) 송신
     addr += 256
4. UART 비활성화 → 인터럽트 벡터 복원 → jmp 0x0
```

---

## 3. 페이지 수신 로직

```c
#define PAGE_SIZE   256
#define END_SIGNAL  0xff
#define ACK         0x06

bool recv_page(uint8_t *buf) {
    buf[0] = USART_Receive();          // 첫 byte 수신

    if (buf[0] == END_SIGNAL)          // 0xFF면 전송 종료
        return false;

    for (uint16_t i = 1; i < PAGE_SIZE; i++)  // 나머지 255B
        buf[i] = USART_Receive();
    return true;
}
```

| 반환 | 의미 |
| --- | --- |
| `false` | 첫 byte가 0xFF → 더 보낼 페이지 없음 (종료) |
| `true` | 256 byte 페이지 수신 완료 |

> 종료 신호가 `0xFF`인 이유: 빈 플래시의 기본값이 0xFF이고, 정상 펌웨어 페이지의 첫 byte가 0xFF일 가능성이 낮다는 가정. (프로젝트 5에서는 명시적 MSG_END로 개선)

---

## 4. 메인 루프

```c
int main(void) {
    DDRC = 0xFF;
    UART_init();

    /* 시작 표시 */
    for (uint8_t i = 0; i < 3; i++) { PORTC = 0x0F; _delay_ms(500); PORTC = 0x00; _delay_ms(500); }

    uint8_t buf[PAGE_SIZE];
    uint32_t write_addr = 0x0000;

    /* 호스트에 준비 완료 통지 */
    USART_Transmit('R'); USART_Transmit('E'); USART_Transmit('A');
    USART_Transmit('D'); USART_Transmit('Y');
    USART_Transmit('\r'); USART_Transmit('\n');

    while (1) {
        if (!recv_page(buf)) {         // 종료 신호
            USART_Transmit(ACK);
            break;
        }
        flash_page_write(write_addr, buf);   // 기록
        USART_Transmit(ACK);                 // 페이지 ACK
        write_addr += PAGE_SIZE;
    }

    /* 점프 준비 */
    UCSR0B = 0x00;                     // UART 비활성화
    cli();
    MCUCR = (1 << IVCE);               // 벡터 변경 활성화
    MCUCR = 0x00;                      // 벡터 → 응용 영역
    asm volatile("jmp 0x0");           // 응용 영역 점프
}
```

호스트는 `READY`를 받으면 페이지를 순차 송신하고, 각 페이지마다 MCU의 ACK를 기다린 뒤 다음 페이지를 보낸다.

---

## 5. UART 드라이버

폴링 방식의 단순 UART다.

```c
void UART_init(void) {
    UBRR0H = 0;
    UBRR0L = 103;                      // 16MHz, 9600 baud
    UCSR0B = (1<<RXEN0) | (1<<TXEN0);  // 송수신 enable
    UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);// 8N1
}

void USART_Transmit(unsigned char data) {
    while (!(UCSR0A & (1<<UDRE0)));    // 송신 버퍼 빌 때까지
    UDR0 = data;
}

unsigned char USART_Receive(void) {
    while (!(UCSR0A & (1<<RXC0)));     // 수신 완료까지 블로킹
    return UDR0;
}
```

| 항목 | 값 |
| --- | --- |
| 보레이트 | 9600 (UBRR=103 @ 16MHz) |
| 방식 | 폴링 (인터럽트 없음) |
| 채널 | UART0 단일 |

> 폴링 수신은 연속 데이터에서 오버런 위험이 있다. 프로젝트 7에서 인터럽트 + 원형큐로 개선된다.

---

## 6. 응용 영역 점프

점프 전 두 가지를 처리한다.

```c
UCSR0B = 0x00;          // UART 끄기 (응용이 깨끗한 상태로 시작)
MCUCR = (1 << IVCE);    // Interrupt Vector Change Enable
MCUCR = 0x00;           // IVSEL=0 → 벡터를 응용 영역으로 복원
asm volatile("jmp 0x0");
```

부트로더가 인터럽트 벡터를 부트 영역으로 옮겼다면, 점프 전 다시 응용 영역으로 되돌려야 응용 프로그램의 인터럽트가 정상 동작한다.

---

## 7. 한계와 다음 단계

| 한계 | 다음 단계 개선 |
| --- | --- |
| 무결성 검증 없음 | 5) CRC16 추가 |
| 종료를 0xFF로 추정 | 5) 명시적 MSG_END |
| 오류 시 재전송 불가 | 5) NAK + 재전송 |
| 폴링 수신 (오버런) | 7) 인터럽트 + 원형큐 |
| UART 직결만 | 7) BLE 무선화 |

---

## 8. 파일 구성

| 파일 | 설명 |
| --- | --- |
| `main.c` | recv_page + flash 기록 + 점프 |
| `uart.c` / `uart.h` | 폴링 UART 드라이버 |
| `library.c` | 빌드용 스텁 |

> 입력 bin은 Python으로 hex → bin 변환하여 준비한다. 호스트는 `READY` 수신 후 256B씩 송신하고, 마지막에 0xFF 한 byte로 종료를 알린다.
