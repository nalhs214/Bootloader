# NU-HC test

BLE 모듈 **NU-HC(nRF54L05)의 UART ↔ BLE 투명 전송을 검증**하는 테스트 코드다. MCU의 두 UART를 브리지로 연결하여, PC ↔ NU-HC(BLE) 간 데이터가 양방향으로 정상 전달되는지 확인한다. 부트로더를 BLE로 무선화(프로젝트 7)하기 전, **BLE 전송 경로 자체를 검증**하는 단계다.

> 환경: ATmega1281 (16 MHz) · UART0(PC) + UART1(NU-HC) 19200 8N1 · NU-HC(nRF54L05)

---

## 목차

1. [목적과 위치](#1-목적과-위치)
2. [브리지 아키텍처](#2-브리지-아키텍처)
3. [동작 코드](#3-동작-코드)
4. [검증 시나리오](#4-검증-시나리오)
5. [사전 조건](#5-사전-조건)
6. [트러블슈팅](#6-트러블슈팅)
7. [파일 구성](#7-파일-구성)

---

## 1. 목적과 위치

프로젝트 7(BLE 부트로더)로 가기 전, "NU-HC가 UART로 받은 데이터를 BLE로 제대로 흘리는가"를 독립적으로 확인한다. 부트로더 로직 없이 **순수 전송 경로만** 검증하므로, 문제가 생겼을 때 BLE 구간인지 부트로더 로직인지 분리할 수 있다.

```text
부트로더 검증의 분리:
  NU-HC test  → BLE 전송 경로가 정상인가?   (이 프로젝트)
  7) BLE-UART → 부트로더 로직이 정상인가?
```

---

## 2. 브리지 아키텍처

MCU를 **UART0 ↔ UART1 양방향 브리지**로 동작시킨다.

```text
        UART0 (PE0/PE1)              UART1 (PD2/PD3)
PC ◀──────────────────▶ ATmega1281 ◀──────────────────▶ NU-HC ··· BLE ··· 상대
   터미널 입력/출력      브리지 중계         투명 전송      (핸드폰/다른 NU-HC)

흐름 A: PC → UART0 → [MCU] → UART1 → NU-HC → BLE 송신
흐름 B: BLE 수신 → NU-HC → UART1 → [MCU] → UART0 → PC 표시
```

PC에서 친 문자가 BLE 너머 상대에 뜨면 송신 경로(A), 상대가 보낸 문자가 PC에 뜨면 수신 경로(B)가 정상이다.

---

## 3. 동작 코드

```c
#include "uart.h"

int main(void) {
    DDRC = 0xFF; PORTC = 0x00;

    UART0_init();          // PC 시리얼
    UART1_init();          // NU-HC
    sei();                 // 인터럽트 enable (RX/TX ISR 동작 필수)

    /* 시작 표시 */
    USART0_Transmit('B');  // Bridge ready
    USART0_Transmit('\r'); USART0_Transmit('\n');

    while (1) {
        /* A: PC(UART0) → NU-HC(UART1) → BLE 송신 */
        if (USART0_Available()) {
            uint8_t c = USART0_Receive();
            USART1_Transmit(c);
            PORTC ^= 0x01;             // 전달 시 LED 토글
        }
        /* B: BLE 수신 → NU-HC(UART1) → PC(UART0) 표시 */
        if (USART1_Available()) {
            uint8_t c = USART1_Receive();
            USART0_Transmit(c);
        }
    }
}
```

| 요소 | 역할 |
| --- | --- |
| `'B'` 출력 | 브리지 시작 확인 (UART0 터미널) |
| 흐름 A | PC 입력을 BLE로 송신 |
| 흐름 B | BLE 수신을 PC에 표시 (양방향 확인) |
| `PORTC ^= 0x01` | 데이터 전달 시각화 |

> 인터럽트 + 원형큐 UART 드라이버(프로젝트 7과 동일)를 사용한다. `sei()`가 없으면 ISR이 동작하지 않아 아무것도 전달되지 않는다.

---

## 4. 검증 시나리오

| 단계 | 확인 |
| --- | --- |
| 1. 브리지 시작 | PC 터미널에 `'B'` 출력 |
| 2. BLE 연결 | NU-HC가 상대(핸드폰 nRF Toolbox 등)와 연결 + Throughput |
| 3. 송신(A) | PC에서 문자 입력 → 상대 BLE에 표시 |
| 4. 수신(B) | 상대 BLE에서 입력 → PC 터미널에 표시 |
| 5. LED | 전달마다 PORTC LED 토글 |

3·4가 모두 되면 NU-HC의 양방향 투명 전송이 정상이며, 프로젝트 7의 부트로더를 올릴 준비가 된 것이다.

---

## 5. 사전 조건

| 항목 | 내용 |
| --- | --- |
| Throughput Mode | NU-HC가 Throughput이어야 raw 데이터가 BLE로 전달됨 |
| BLE 연결 | 연결된 상태여야 함 (미연결 시 Command Mode) |
| 보레이트 | UART1 ↔ NU-HC 일치 (19200) |
| 배선 | NU-HC TX → MCU PD2, NU-HC RX → MCU PD3, GND 공통 |

---

## 6. 트러블슈팅

| 증상 | 원인 | 해결 |
| --- | --- | --- |
| PC에 `ERROR` 수신 | NU-HC Command Mode (미연결) | BLE 연결 + Throughput 진입 |
| 아무것도 전달 안 됨 | `sei()` 누락 / 보레이트 불일치 | 인터럽트 enable, 속도 확인 |
| 입력이 안 보임 | 시리얼 터미널 로컬 에코 없음 | MCU 응답(B)으로 통신 확인 |
| 글자 깨짐 | UART1↔NU-HC 보레이트 불일치 | 양측 19200 통일 |

> NU-HC Command/Throughput 전환은 `+++`(앞뒤 1초 guard)로 Command 복귀, `AT+TPMODE=1`로 Throughput 진입.

---

## 7. 파일 구성

| 파일 | 설명 |
| --- | --- |
| `main.c` | UART0 ↔ UART1 양방향 브리지 |
| `uart.c` / `uart.h` | 인터럽트 UART 드라이버 (프로젝트 7과 동일 계열) |
| `library.c` | 빌드용 스텁 |

> 이 브리지로 BLE 경로를 검증한 뒤, 같은 UART 드라이버 위에 부트로더 로직을 얹은 것이 프로젝트 7이다.
