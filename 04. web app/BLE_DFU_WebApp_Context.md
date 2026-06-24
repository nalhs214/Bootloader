# BLE DFU Web App — AI Context Document

> VS Code AI에 전달하는 컨텍스트 문서입니다.
> 이 파일을 읽고 아래 명세에 따라 Web App을 구현해주세요.

---

## 1. 프로젝트 개요

ATmega1281 MCU에 BLE를 통해 펌웨어 bin 파일을 무선으로 업로드하는
**Web App 기반 DFU(Device Firmware Update) Sender**를 구현합니다.

### 시스템 구성

```
[Chrome Web App]
     |
     | Web Bluetooth API (BLE)
     v
[NU-HC #1 — nRF54L05, BLE Central]
     |
     | BLE Transparent Mode (NUS)
     v
[NU-HC #2 — nRF54L05, BLE Peripheral]
     |
     | UART 19200 bps
     v
[ATmega1281 — 커스텀 부트로더]
```

- Chrome이 NU-HC #1에 BLE로 연결하면 **자동으로 Transparent Mode 진입**
- AT CMD 전송 불필요
- NU-HC는 BLE ↔ UART 투명 중계기 역할
- 실제 DFU 로직은 ATmega1281 부트로더와 직접 통신

---

## 2. 프로젝트 파일 구조

```
bl-web-sender/
├── index.html       ← UI (파일 선택, 버튼, 진행률, 로그창)
├── ble.js           ← BLE 연결 / NUS characteristic 바인딩
├── protocol.js      ← 패킷 빌더 / CRC16 / ACK 핸들러  ← 핵심
└── main.js          ← 전체 흐름 제어 (핸드셰이크 → 전송 → 완료)
```

> `protocol.js`는 순수 함수로만 구성합니다.
> BLE나 DOM에 의존하지 않아야 나중에 C++ 이식이 쉽습니다.

---

## 3. BLE 연결 명세

### NUS (Nordic UART Service) UUID

```javascript
const NUS_SERVICE_UUID      = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_TX_CHAR_UUID      = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'; // Write (PC→MCU)
const NUS_RX_CHAR_UUID      = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'; // Notify (MCU→PC)
```

### Web Bluetooth API 연결 순서

```javascript
// 1. BLE 디바이스 스캔 및 연결
const device = await navigator.bluetooth.requestDevice({
    filters: [{ services: [NUS_SERVICE_UUID] }]
});

// 2. GATT 서버 연결
const server  = await device.gatt.connect();
const service = await server.getPrimaryService(NUS_SERVICE_UUID);

// 3. Characteristic 바인딩
const txChar = await service.getCharacteristic(NUS_TX_CHAR_UUID); // write
const rxChar = await service.getCharacteristic(NUS_RX_CHAR_UUID); // notify

// 4. Notify 활성화 (ACK 수신용)
await rxChar.startNotifications();
rxChar.addEventListener('characteristicvaluechanged', onRxReceived);
```

### 주의사항

- Web Bluetooth는 **Chrome 데스크탑 전용** (iOS 미지원, Firefox 미지원)
- `localhost` 또는 `https://` 에서만 동작 (`file://` 불가)
- VS Code Live Server로 실행할 것

---

## 4. 통신 프로토콜 명세

### 4-1. 패킷 구조 (262바이트 고정)

```
Offset  Size  Field         Value
------  ----  ----------    -------------------------
0       1     START_CODE    0xEF (고정)
1       1     MSG_TYPE      0x01=DATA / 0x02=END
2~3     2     DATA_LENGTH   256(0x00,0x01) LE / END시 0(0x00,0x00)
4~259   256   DATA          bin 데이터 (부족 시 0xFF 패딩)
260~261 2     CRC16         little-endian
```

### 4-2. 상수 정의

```javascript
const START_CODE  = 0xEF;
const MSG_DATA    = 0x01;
const MSG_END     = 0x02;
const ACK         = 0x06;
const NAK         = 0x15;
const PAGE_SIZE   = 256;   // 데이터 필드 크기
const PKT_SIZE    = 262;   // 전체 패킷 크기 (1+1+2+256+2)
const CHUNK_SIZE  = 20;    // BLE MTU 제약 (23 - 3 overhead)
const CHUNK_DELAY = 20;    // 청크 간 지연 ms (NU-HC flush: ~16ms)
const MAX_RETRY   = 3;     // ACK 실패 시 최대 재전송 횟수
const ACK_TIMEOUT = 5000;  // ACK 대기 타임아웃 ms
```

### 4-3. CRC16 알고리즘

```
알고리즘: CRC16-CCITT-FALSE
Polynomial: 0x1021
Initial:    0xFFFF
Input:      MSG_TYPE(1) + DATA_LENGTH(2) + DATA(256) = 총 259바이트
출력:       little-endian으로 패킷 [260~261]에 삽입
```

```javascript
// protocol.js
function crc16(data) {
    // data: Uint8Array
    let crc = 0xFFFF;
    for (let i = 0; i < data.length; i++) {
        crc ^= (data[i] << 8);
        for (let j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
            crc &= 0xFFFF;
        }
    }
    return crc;
}
```

### 4-4. 패킷 빌더

```javascript
// protocol.js
function makePacket(msgType, pageData) {
    // pageData: Uint8Array (최대 256바이트, 부족 시 0xFF 패딩)
    const pkt = new Uint8Array(PKT_SIZE);

    // Header
    pkt[0] = START_CODE;
    pkt[1] = msgType;

    // DATA_LENGTH (little-endian)
    const dataLen = (msgType === MSG_DATA) ? PAGE_SIZE : 0;
    pkt[2] = dataLen & 0xFF;
    pkt[3] = (dataLen >> 8) & 0xFF;

    // DATA (256바이트, 0xFF 패딩)
    pkt.fill(0xFF, 4, 260);
    if (pageData && pageData.length > 0) {
        pkt.set(pageData.slice(0, PAGE_SIZE), 4);
    }

    // CRC16: MSG_TYPE(1) + DATA_LENGTH(2) + DATA(256)
    const crcBuf = new Uint8Array(3 + PAGE_SIZE);
    crcBuf[0] = pkt[1];
    crcBuf[1] = pkt[2];
    crcBuf[2] = pkt[3];
    crcBuf.set(pkt.slice(4, 260), 3);

    const crc = crc16(crcBuf);
    pkt[260] = crc & 0xFF;         // CRC low byte
    pkt[261] = (crc >> 8) & 0xFF;  // CRC high byte

    return pkt;
}
```

---

## 5. 전송 절차

### 5-1. 전체 흐름

```
BLE 연결
  └→ Notify 활성화
       └→ 핸드셰이크 ('S' 전송 → 'R' 수신 대기)
            └→ 업로드 루프 (페이지 단위)
                 ├→ 패킷 빌드 (makePacket)
                 ├→ 20바이트 청크 분할
                 ├→ 청크 BLE 전송 (20ms 간격)
                 └→ ACK 대기 (최대 5초, 재시도 3회)
                      └→ 전체 완료 시 MSG_END 전송
```

### 5-2. 핸드셰이크

```javascript
// main.js
async function handshake(txChar) {
    // 1. 'S' (0x53) 전송
    await txChar.writeValueWithoutResponse(new Uint8Array([0x53]));

    // 2. 'R' (0x52) 수신 대기 (최대 30초, 200ms 간격 재전송)
    // → ATmega 부트로더가 'R'로 응답하면 업로드 시작
    // → Notify 콜백에서 0x52 수신 시 resolve
}
```

**MCU 부트로더 동작:**
- 10초 타임아웃 루프에서 'S' 수신 대기
- 'S' 수신 시 RxFlush 후 'R' 응답
- 이후 `run_bootloader()` 진입

### 5-3. 청크 분할 전송

```javascript
// protocol.js
async function sendChunked(txChar, packet) {
    // 262바이트 패킷을 20바이트 단위로 분할 전송
    // 총 14청크: 13×20B + 1×2B
    let offset = 0;
    while (offset < packet.length) {
        const chunk = packet.slice(offset, offset + CHUNK_SIZE);
        await txChar.writeValueWithoutResponse(chunk);
        await sleep(CHUNK_DELAY); // 20ms 대기 (NU-HC 16ms flush 고려)
        offset += CHUNK_SIZE;
    }
}
```

### 5-4. ACK 수신 및 재전송 로직

```javascript
// protocol.js
async function sendPacketWithAck(txChar, packet) {
    for (let retry = 0; retry < MAX_RETRY; retry++) {
        await sendChunked(txChar, packet);

        // ACK 대기 (Promise + timeout)
        const result = await waitForAck(ACK_TIMEOUT);

        if (result === ACK)  return true;   // 성공
        if (result === NAK)  continue;      // 재시도
        if (result === null) continue;      // 타임아웃 → 재시도
    }
    return false; // 3회 실패
}

// Notify 콜백에서 ACK/NAK 수신
function onRxReceived(event) {
    const byte = new Uint8Array(event.target.value.buffer)[0];
    // byte === 0x06 → ACK
    // byte === 0x15 → NAK
    resolveAck(byte); // Promise resolve
}
```

**MCU ACK 타이밍:**
- 전체 패킷(262바이트) 수신 완료 후 `_delay_ms(100)` (100ms 대기)
- CRC 검증 성공 → ACK (0x06) 전송
- CRC 검증 실패 → NAK (0x15) 전송

### 5-5. 업로드 루프

```javascript
// main.js
async function upload(binArray, txChar) {
    const pageCount = Math.ceil(binArray.length / PAGE_SIZE);

    for (let i = 0; i < pageCount; i++) {
        const pageData = binArray.slice(i * PAGE_SIZE, (i + 1) * PAGE_SIZE);

        // 1. 패킷 빌드
        const pkt = makePacket(MSG_DATA, pageData);

        // 2. 전송 + ACK 대기
        const ok = await sendPacketWithAck(txChar, pkt);
        if (!ok) throw new Error(`페이지 ${i+1}/${pageCount} 전송 실패`);

        updateProgress(i + 1, pageCount);
    }

    // 3. END 패킷 전송
    const endPkt = makePacket(MSG_END, null);
    const ok = await sendPacketWithAck(txChar, endPkt);
    if (!ok) throw new Error('END 패킷 전송 실패');
    // MCU는 ACK 후 App으로 점프
}
```

---

## 6. UI 구성 요소 (index.html)

```
[ BLE 연결 버튼    ]  ← navigator.bluetooth.requestDevice()
[ bin 파일 선택    ]  ← <input type="file" accept=".bin">
[ 전송 시작 버튼   ]  ← 연결 + 파일 선택 후 활성화
[ 진행률 바        ]  ← 현재 페이지 / 전체 페이지
[ HEX 로그 출력창  ]  ← 패킷 HEX dump, ACK/NAK, 에러 표시
```

---

## 7. 디버깅 포인트

### HEX dump 출력 (검증용)

```javascript
// 패킷 첫 10바이트 + 마지막 4바이트 로그 출력
function logPacket(pkt, pageIdx) {
    const head = Array.from(pkt.slice(0, 10))
        .map(b => b.toString(16).padStart(2, '0').toUpperCase())
        .join(' ');
    const tail = Array.from(pkt.slice(258, 262))
        .map(b => b.toString(16).padStart(2, '0').toUpperCase())
        .join(' ');
    console.log(`[PAGE ${pageIdx}] ${head} ... ${tail}`);
}

// 예상 출력 (LED_blink.bin 첫 페이지):
// [PAGE 1] EF 01 00 01 XX XX XX XX XX XX ... XX XX CRC_L CRC_H
```

### 검증 절차

1. `makePacket()` 출력 HEX가 `bl_sender` 출력과 일치하는지 확인
2. CRC16 계산값이 MCU 수신측과 일치하는지 확인
3. ATmega1281 PORTC LED가 각 페이지 수신마다 토글되는지 확인
4. 5페이지 완료 후 LED_blink 동작 확인

---

## 8. 참고: 실제 동작 검증 완료 항목

아래는 기존 `bl_sender` CLI로 검증이 완료된 동작입니다.
Web App에서도 동일하게 구현해야 합니다.

| 항목 | 검증된 값 |
|------|-----------|
| UART 보드레이트 | 19200 bps |
| 청크 크기 | 20바이트 |
| 청크 간격 | 20ms |
| 패킷 크기 | 262바이트 |
| CRC 범위 | MSG_TYPE + DATA_LENGTH + DATA (259바이트) |
| CRC 알고리즘 | CRC16 CCITT-FALSE (poly=0x1021, init=0xFFFF) |
| CRC 바이트 순서 | little-endian |
| 데이터 패딩 | 0xFF |
| ACK 값 | 0x06 |
| NAK 값 | 0x15 |
| 최대 재시도 | 3회 |
| 테스트 파일 | LED_blink.bin (1050바이트, 5페이지) |

---

## 9. 환경 설정

```
브라우저: Chrome 데스크탑 (Web Bluetooth 지원)
에디터:  VS Code + Live Server 확장
실행:    Live Server → http://localhost:5500
chrome://flags/#enable-web-bluetooth → Enabled 확인
```

---

*소스 기반: MCU__BLE-UART_Bootloader / Linux__BLE-UART_Bootloader*
*타겟: ATmega1281, NU-HC (nRF54L05)*

---

## 10. 구현 확정 사항 (2026-06-19)

- DATA와 END 패킷 모두 `DATA_LENGTH=256` (`0x00, 0x01` little-endian)
- END 패킷도 256바이트 DATA 영역을 포함하며 `0xFF`로 채움 (전체 262바이트)
- ACK (`0x06`)와 NAK (`0x15`)는 단일 바이트 Notify
- ATmega1281 부트로더 영역은 4096 words (8192 bytes)
- 애플리케이션 `.bin` 최대 크기는 120 KiB (122,880 bytes)
- NU-HC는 NUS Service UUID를 광고하므로 service filter로 검색
- 연결이 끊긴 뒤 페이지 단위 이어 보내기는 지원하지 않으며 다음 전송은 처음부터 시작
