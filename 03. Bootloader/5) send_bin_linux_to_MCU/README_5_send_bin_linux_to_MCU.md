# 5) send_bin_linux_to_MCU

**UART 방식 부트로더의 완성형**이다. 프로젝트 3의 단순 페이지 수신에 **패킷 프레이밍·CRC16 무결성 검증·ACK/NAK 재전송**을 더해 신뢰성 있는 부트로더를 만들고, Linux 호스트 송신 도구(`bl_sender`)를 함께 구현한다. BLE 이전 단계로 호스트와 MCU를 UART로 직접 연결한다.

> 환경: ATmega1281 (16 MHz) · UART 19200 8N1 · Ubuntu(VMware) gcc

---

## 목차

1. [아키텍처](#1-아키텍처)
2. [패킷 프로토콜](#2-패킷-프로토콜)
3. [MCU 측 동작](#3-mcu-측-동작)
4. [호스트(bl_sender) 측 동작](#4-호스트bl_sender-측-동작)
5. [CRC16](#5-crc16)
6. [전송 시퀀스](#6-전송-시퀀스)
7. [빌드·실행](#7-빌드실행)
8. [파일 구성](#8-파일-구성)

---

## 1. 아키텍처

```text
┌──────────────────────┐         ┌──────────────────────┐
│  Linux Host          │  UART   │  ATmega1281           │
│  bl_sender           │ 19200   │  Bootloader           │
│  ┌────────────────┐  │ 8N1     │  ┌────────────────┐   │
│  │ make_packet    │  │◀───────▶│  │ recv_packet    │   │
│  │ send_packet    │  │ DATA→   │  │ CRC 검증        │   │
│  │ upload         │  │ ←ACK    │  │ flash_page_write│   │
│  └────────────────┘  │         │  │ jump_to_app    │   │
└──────────────────────┘         └──────────────────────┘
```

| 측 | 위치 | 파일 |
| --- | --- | --- |
| MCU | `5) send_bin_linux_to_MCU/` | `main.c`, `protocol.c`, `boot.c`, `uart.c` |
| Host | `Linux code/bl_sender2/` | `src/main.c`, `uart.c`, `packet.c` |

---

## 2. 패킷 프로토콜

프로젝트 3의 raw 256B에서 **헤더 + CRC**를 갖춘 262B 고정 패킷으로 발전했다.

```text
┌──────┬──────┬──────────┬───────────────┬────────┐
│START │ TYPE │  LENGTH  │     DATA      │ CRC16  │
│ 0xEF │ 1B   │  2B(LE)  │    256B       │ 2B(LE) │
└──────┴──────┴──────────┴───────────────┴────────┘
  0      1      2~3         4~259          260~261     = 262B
```

| 메시지 | 값 | 의미 |
| --- | --- | --- |
| MSG_DATA | 0x01 | 데이터 페이지 |
| MSG_END | 0x02 | 전송 종료 → 점프 |
| ACK | 0x06 | 정상 |
| NAK | 0x15 | 오류 → 재전송 |

---

## 3. MCU 측 동작

### run_bootloader

```c
void run_bootloader(void) {
    bl_data_packet_t pkt;
    uint32_t write_addr = 0x0000;

    while (1) {
        uint8_t result = recv_packet(&pkt);

        if (result == MSG_DATA) {
            if (write_addr >= BOOT_START) {    // 부트 영역 보호
                USART_Transmit(NAK); break;
            }
            flash_page_write(write_addr, pkt.data);
            USART_Transmit(ACK);
            write_addr += PAGE_SIZE;
            PORTC ^= 0x01;                     // 진행 표시
        }
        else if (result == MSG_END) {
            USART_Transmit(ACK);
            _delay_ms(100);
            jump_to_app();                     // 응용 점프
        }
        else {
            USART_Transmit(NAK);               // CRC 실패 등
        }
    }
}
```

### main: 부트 진입 + 앱 유효성 검사

```c
int main(void) {
    DDRC = 0xFF; PORTC = 0x00;
    UART0_init();

    uint16_t timeout = 3000;
    while (timeout--) {                 // 부트 대기
        PORTC = 0x05; _delay_ms(1);
        if (USART_Available()) {        // 데이터 도착 → 부트로더 진입
            run_bootloader();
            return 0;
        }
    }

    /* 타임아웃: 응용 영역에 코드 있으면 점프 */
    if (pgm_read_word(0x0000) != 0xFFFF) {
        for (uint8_t i = 0; i < 5; i++) { PORTC = 0x0F; _delay_ms(200); PORTC = 0x00; _delay_ms(200); }
        jump_to_app();
    }
    while (1);
}
```

> 프로젝트 3과 달리 `pgm_read_word(0x0000) != 0xFFFF`로 **응용 영역에 유효한 코드가 있는지** 확인 후 점프한다.

### recv_packet (START_CODE 1회 확인)

```c
uint8_t recv_packet(bl_data_packet_t *pkt) {
    uint8_t raw[PKT_SIZE];
    for (uint16_t i = 0; i < PKT_SIZE; i++)
        raw[i] = USART_Receive();

    if (raw[0] != START_CODE)           // 정렬 확인 (1회)
        return NAK;

    /* 파싱 + CRC 검증 → msg_type 또는 NAK 반환 */
}
```

> 이 단계는 START_CODE를 1회만 확인한다. BLE 환경의 잔재·노이즈 대응(재동기 루프)은 프로젝트 7에서 추가된다.

---

## 4. 호스트(bl_sender) 측 동작

```c
int upload(int bin_fd, int page_count) {
    for (int i = 0; i < page_count; i++) {
        read(bin_fd, page_buf, PAGE_SIZE);        // 256B 읽기
        make_packet(&pkt, MSG_DATA, page_buf, PAGE_SIZE);
        if (!send_packet(&pkt)) return 1;         // 전송 + ACK 대기
    }
    make_packet(&pkt, MSG_END, NULL, 0);          // 종료
    send_packet(&pkt);
}
```

`send_packet`은 패킷을 보내고 ACK를 기다리며, NAK/타임아웃 시 `MAX_RETRY`(3회)까지 재전송한다.

---

## 5. CRC16

호스트와 MCU가 **동일 구현**을 사용해야 한다.

```c
uint16_t crc16(uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;                        // init
    for (uint16_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);  // poly
    }
    return crc;
}
```

| 항목 | 값 |
| --- | --- |
| 알고리즘 | CRC-16/CCITT-FALSE |
| Polynomial | 0x1021 |
| Init | 0xFFFF |
| 계산 범위 | MSG_TYPE + DATA_LENGTH + DATA(256) = 259B |

---

## 6. 전송 시퀀스

```text
Host: bin 로드 → 페이지 N개
for page in 1..N:
    Host ──MSG_DATA(262B)──▶ MCU
    MCU: 수신 → CRC 검증
         ├ 일치: flash 기록 → ──ACK──▶ Host → 다음 페이지
         └ 불일치: ──NAK──▶ Host → 재전송(≤3)
Host ──MSG_END──▶ MCU ──ACK──▶ Host
MCU: jump_to_app()
```

---

## 7. 빌드·실행

```bash
# MCU: Microchip Studio에서 빌드 후 부트 영역에 플래시 (J-Link)

# Host: bl_sender 빌드
cd "Linux code/bl_sender2"
gcc -Iinc -o bl_sender src/*.c

# 실행
./bl_sender <firmware.bin> /dev/ttyUSB0
```

| 항목 | 값 |
| --- | --- |
| 보레이트 | 19200 (16MHz에서 115200은 오차로 깨짐) |
| 포트 | `/dev/ttyUSB0` (CP210x) |
| 페이지 | 256 byte |

---

## 8. 파일 구성

| 파일 | 설명 |
| --- | --- |
| `main.c` | 부트 진입·`run_bootloader`·앱 유효성 검사 |
| `protocol.c/.h` | `crc16`, `recv_packet` (파싱·검증) |
| `boot.c/.h` | `flash_page_write`, `jump_to_app` |
| `uart.c/.h` | 폴링 UART 드라이버 |
| `Linux code/bl_sender2/` | 호스트 송신 도구 (`make_packet`, `send_packet`, `upload`) |
| `todo list (디버깅 순서).txt` | 개발·디버깅 메모 |
| `specs.c` | 사양 메모 |

> 이 UART 부트로더에 BLE(NU-HC), 인터럽트 큐, 핸드셰이크, 재동기를 더한 것이 프로젝트 7이다.
