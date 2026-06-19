# 2) Flash_Read_Write

부트로더가 펌웨어를 기록하기 위한 핵심 기능인 **플래시 페이지 단위 read/write**를 구현·검증한다. AVR의 SPM(Store Program Memory) 명령으로 플래시를 erase → fill → write하는 3단계를 다룬다.

> 환경: ATmega1281 (16 MHz) · `<avr/boot.h>` · 페이지 256 byte (128 word)

---

## 목차

1. [플래시 기록의 제약](#1-플래시-기록의-제약)
2. [3단계 기록 절차](#2-3단계-기록-절차)
3. [코드 분석](#3-코드-분석)
4. [word 단위 fill (little-endian)](#4-word-단위-fill-little-endian)
5. [SPM 완료 대기와 RWW](#5-spm-완료-대기와-rww)
6. [동작 흐름](#6-동작-흐름)
7. [파일 구성](#7-파일-구성)

---

## 1. 플래시 기록의 제약

AVR 플래시는 RAM처럼 임의 번지에 바로 쓸 수 없다. 다음 제약이 있다.

```text
- erase/write는 페이지(256B) 단위로만 가능
- write 전에 반드시 해당 페이지를 erase 해야 함
- fill은 word(2B) 단위로 임시 페이지 버퍼에 채운 뒤 write로 커밋
- SPM 명령은 부트 영역 코드에서만 실행 가능
```

따라서 "1 byte 수정"도 페이지 전체를 erase → fill → write 해야 한다.

---

## 2. 3단계 기록 절차

```text
flash_page_write(addr, buf):
   ┌─ 1. erase ─────────────────────────┐
   │  boot_page_erase(addr)             │  페이지 지움
   │  while(SPMCSR & (1<<SPMEN));        │  완료 대기
   │  boot_rww_enable();                │  읽기 재활성화
   ├─ 2. fill ──────────────────────────┤
   │  for i in 0,2,..254:               │  word 단위로
   │    word = buf[i] | (buf[i+1]<<8)   │  little-endian 조합
   │    boot_page_fill(addr+i, word)    │  임시 버퍼에 채움
   ├─ 3. write ─────────────────────────┤
   │  boot_page_write(addr)             │  플래시에 커밋
   │  while(SPMCSR & (1<<SPMEN));        │  완료 대기
   │  boot_rww_enable();                │  읽기 재활성화
   └────────────────────────────────────┘
```

---

## 3. 코드 분석

```c
#define PAGE_SIZE 256
#define TEST_ADDR  0x0000UL

/* 1단계: 페이지 지우기 */
void flash_erase(uint32_t addr) {
    boot_page_erase(addr);
    while (SPMCSR & (1 << SPMEN));   // SPM 완료 대기
    boot_rww_enable();
}

/* 2단계: word(2B) 단위로 임시 버퍼 채우기 */
void flash_fill(uint32_t addr, uint8_t *buf) {
    for (uint16_t i = 0; i < PAGE_SIZE; i += 2) {
        uint16_t word = buf[i] | ((uint16_t)buf[i+1] << 8);  // little-endian
        boot_page_fill(addr + i, word);
    }
}

/* 3단계: 플래시에 기록 */
void flash_write(uint32_t addr) {
    boot_page_write(addr);
    while (SPMCSR & (1 << SPMEN));
    boot_rww_enable();
}

/* 통합: erase → fill → write (인터럽트 차단) */
void flash_page_write(uint32_t addr, uint8_t *buf) {
    cli();
    flash_erase(addr);
    while (SPMCSR & (1 << SPMEN));
    flash_fill(addr, buf);
    flash_write(addr);
    sei();
}
```

테스트 데이터는 `0,1,2,...,255` 패턴으로 채워 기록 후 검증한다.

```c
void make_test_data(uint8_t *buf) {
    for (uint16_t i = 0; i < PAGE_SIZE; i++)
        buf[i] = (uint8_t)(i & 0xFF);
}
```

---

## 4. word 단위 fill (little-endian)

`boot_page_fill`은 word(16비트) 단위로 받는다. 256 byte 버퍼를 128개 word로 조합한다.

```c
uint16_t word = buf[i] | ((uint16_t)buf[i+1] << 8);
//               하위 byte        상위 byte
// buf[0]=0x12, buf[1]=0x34 → word=0x3412 (little-endian)
```

> AVR은 little-endian이므로 낮은 주소 byte가 word의 하위 byte가 된다. 호스트가 보내는 바이너리도 동일 순서여야 일치한다.

---

## 5. SPM 완료 대기와 RWW

| 구문 | 역할 |
| --- | --- |
| `while(SPMCSR & (1<<SPMEN));` | SPM 명령이 끝날 때까지 대기 (필수) |
| `boot_rww_enable();` | RWW(Read-While-Write) 영역 읽기 재활성화 |
| `cli()` / `sei()` | 기록 중 인터럽트 차단 (타이밍 보호) |

SPM 진행 중에는 해당 영역을 읽을 수 없으므로, 기록 후 `boot_rww_enable()`로 다시 읽기 가능 상태로 만든다.

---

## 6. 동작 흐름

```text
1. PORTC LED 3회 점멸 (시작 표시)
2. make_test_data로 0~255 패턴 생성
3. TEST_ADDR(0x0000) 페이지 erase
4. flash_page_write로 기록
5. (검증) 디버거나 read로 0~255 패턴 확인
```

---

## 7. 파일 구성

| 파일 | 설명 |
| --- | --- |
| `main.c` | 플래시 erase/fill/write 함수 + 테스트 |
| `library.c` | 빌드용 스텁 |

> 이 프로젝트의 `flash_page_write`는 이후 프로젝트 3·5·7에서 그대로 재사용되어, 수신한 펌웨어 페이지를 기록하는 데 쓰인다.
