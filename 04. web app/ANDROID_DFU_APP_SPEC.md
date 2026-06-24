# Android BLE DFU App 구현 명세

이 문서는 기존 BLE DFU Web App과 동일한 기능을 Android 네이티브 앱으로 구현하기 위한
Codex 전달용 명세다. Android Studio 프로젝트 루트에 이 파일을 복사한 뒤 Codex CLI에 다음과
같이 요청한다.

```text
ANDROID_DFU_APP_SPEC.md를 끝까지 읽고 현재 Android 프로젝트에 명세를 구현해줘.
계획만 작성하지 말고 빌드 가능한 코드, 권한, 테스트까지 완성해줘.
```

## 1. 목표

Kotlin과 Jetpack Compose를 사용하여 다음 기능을 구현한다.

- Nordic UART Service(NUS)를 광고하는 BLE 장치 검색
- 장치 선택, 연결, 연결 해제 및 마지막 장치 재연결
- `.bin` 펌웨어 선택 및 최대 크기 검사
- ATmega1281 부트로더와 `S`/`R` 핸드셰이크
- 256-byte 페이지 단위 펌웨어 전송
- ACK/NAK 처리 및 최대 3회 재전송
- 진행률, 전송 페이지, 재시도 횟수, 경과 시간, 로그 표시
- 사용자 전송 중단 및 BLE 연결 해제 처리

WebView로 기존 웹 페이지를 감싸지 말고 Android BLE GATT API로 네이티브 구현한다.

## 2. 기술 조건

- Language: Kotlin
- UI: Jetpack Compose + Material 3
- 비동기 처리: Kotlin Coroutines, Flow/StateFlow
- 상태 관리: ViewModel
- 파일 선택: Storage Access Framework의 `ActivityResultContracts.OpenDocument`
- BLE 로직과 프로토콜 로직을 UI 코드에서 분리한다.
- 프로젝트에 이미 적용된 Gradle/Compose 버전을 우선 사용한다.
- minSdk는 프로젝트 기본값을 존중하되 BLE 기능에 필요한 버전 분기를 구현한다.

권장 패키지 구조:

```text
app/src/main/java/<package>/
├── MainActivity.kt
├── ble/
│   ├── BleNusClient.kt
│   └── BleModels.kt
├── dfu/
│   ├── DfuProtocol.kt
│   └── DfuUploader.kt
└── ui/
    ├── DfuScreen.kt
    ├── DfuUiState.kt
    └── DfuViewModel.kt
```

## 3. Android 권한

Manifest와 런타임 권한 요청을 모두 구현한다.

Android 12(API 31) 이상:

```xml
<uses-permission
    android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
```

Android 11(API 30) 이하를 지원할 경우 BLE 검색에 필요한 위치 권한을 해당 API 범위에만
적용한다. 권한 거부, Bluetooth 꺼짐, BLE 미지원 상태를 사용자에게 명확히 표시한다.

## 4. BLE 명세

### UUID

| 역할 | UUID |
| --- | --- |
| NUS Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Android -> MCU Write | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| MCU -> Android Notify | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| CCCD | `00002902-0000-1000-8000-00805F9B34FB` |

### 검색과 연결

1. `BluetoothLeScanner`와 NUS Service UUID `ScanFilter`를 사용한다.
2. 중복 장치는 주소 기준으로 합치고 이름, 주소, RSSI를 표시한다.
3. 검색에는 적절한 timeout을 두고 사용자가 중지할 수 있게 한다.
4. 선택한 장치에 `connectGatt()`로 연결한다.
5. 연결 후 `discoverServices()`를 호출한다.
6. NUS Service와 두 characteristic을 확인한다.
7. Notify characteristic에 `setCharacteristicNotification(true)`를 적용한다.
8. CCCD에 notification enable 값 `0x01, 0x00`을 기록한다.
9. CCCD 설정이 성공한 뒤에만 앱 상태를 `연결됨`으로 변경한다.

연결, service discovery, descriptor write, characteristic write는 callback 결과와 status를
확인해야 한다. 오류 또는 연결 해제 시 진행 중인 응답 대기와 업로드를 취소하고 GATT를
정리한다. 연결이 끊긴 전송은 이어 보내지 않고 다음 전송을 처음부터 시작한다.

### BLE 쓰기

- 한 번에 최대 20 bytes만 write한다.
- NUS Write characteristic은 Write Without Response를 우선 사용한다.
- Android GATT 작업은 동시에 여러 개 실행하지 말고 단일 큐에서 순차 처리한다.
- 각 20-byte 청크 사이에 최소 20 ms 간격을 둔다.
- API 33 이상에서는 새 `writeCharacteristic(characteristic, value, writeType)` overload를
  사용하고, 이전 API에서는 호환 분기를 둔다.
- write 요청 자체가 거절되거나 GATT 연결이 끊기면 즉시 전송 실패로 처리한다.

## 5. 부트로더 프로토콜

### 상수

```text
START_CODE       = 0xEF
MSG_DATA         = 0x01
MSG_END          = 0x02
ACK              = 0x06
NAK              = 0x15
HANDSHAKE_START  = 0x53  ('S')
HANDSHAKE_READY  = 0x52  ('R')
PAGE_SIZE        = 256
PACKET_SIZE      = 262
CHUNK_SIZE       = 20
CHUNK_DELAY_MS   = 20
MAX_RETRY        = 3
ACK_TIMEOUT_MS   = 5000
MAX_FIRMWARE     = 122880 bytes (120 KiB)
```

### CRC16

CRC-16/CCITT-FALSE를 사용한다.

```text
Initial value = 0xFFFF
Polynomial    = 0x1021
RefIn         = false
RefOut        = false
XorOut        = 0x0000
```

표준 검증 문자열 `123456789`의 결과는 `0x29B1`이어야 한다.

### 262-byte 패킷

| Offset | Size | 내용 |
| --- | ---: | --- |
| 0 | 1 | `START_CODE` = `0xEF` |
| 1 | 1 | `MSG_DATA(0x01)` 또는 `MSG_END(0x02)` |
| 2 | 2 | 데이터 길이 `256`, little-endian (`00 01`) |
| 4 | 256 | 페이지 데이터, 남는 부분은 `0xFF` padding |
| 260 | 2 | CRC16, little-endian |

CRC 입력 범위는 offset `1`부터 `259`까지다. 즉 `START_CODE`는 제외하고 message type,
length, 256-byte data field를 포함한다. 마지막 페이지와 END 패킷도 데이터 영역을
`0xFF`로 채우고 length는 항상 256으로 기록한다.

### 핸드셰이크

1. Notify 수신 대기를 먼저 준비한다.
2. `S(0x53)` 한 byte를 전송한다.
3. 200 ms 동안 `R(0x52)`을 기다린다.
4. 응답이 없으면 최대 150회 반복한다. 전체 제한은 약 30초다.
5. `R`을 받으면 데이터 페이지 전송을 시작한다.

사용자는 핸드셰이크가 진행되는 동안 ATmega1281을 수동 리셋한다. 로그에는 25회 간격으로
핸드셰이크 전송 횟수를 표시한다.

### 페이지 전송

1. 펌웨어를 256-byte 페이지로 나눈다.
2. `MSG_DATA` 패킷을 생성한다.
3. ACK/NAK 수신 대기를 먼저 준비한다.
4. 262-byte 패킷을 최대 20-byte BLE 청크로 순차 전송한다.
5. 마지막 청크 이후 ACK를 최대 5초 기다린다.
6. ACK이면 다음 페이지로 이동한다.
7. NAK 또는 timeout이면 같은 패킷을 다시 전송한다.
8. 한 패킷은 최초 시도를 포함해 최대 3회 시도한다.
9. 모든 데이터 페이지가 성공하면 `MSG_END` 패킷도 같은 방식으로 전송한다.
10. END의 ACK를 받으면 전송 완료로 처리한다.

Notify payload에 여러 byte가 들어올 수 있으므로 각 byte를 순서대로 검사한다. `0x52`,
`0x06`, `0x15` 이외의 byte도 hex 형식으로 로그에 표시한다.

## 6. 파일 처리

- MIME filter는 `application/octet-stream`을 사용하되 파일명이 `.bin`인지 추가 확인한다.
- 빈 파일은 거부한다.
- 122,880 bytes를 초과하면 거부한다.
- 선택한 파일명, byte 크기, 페이지 수를 표시한다.
- 파일 선택과 읽기는 메인 스레드를 막지 않게 처리한다.
- 전송 중에는 다른 파일 선택을 막는다.

## 7. UI 요구사항

한 화면에서 다음 항목을 제공한다.

- BLE 연결 상태와 선택 장치 이름
- BLE 검색/연결, 재연결, 연결 해제 버튼
- 검색 결과 장치 목록 또는 선택 dialog/bottom sheet
- 펌웨어 파일 선택 영역
- 선택 파일명, 크기, 페이지 수
- 전송 시작 및 중단 버튼
- 진행률 bar와 percent
- 전송 완료 페이지 수/전체 페이지 수
- 누적 재시도 횟수
- 경과 시간
- timestamp가 있는 스크롤 로그와 로그 지우기 버튼

버튼 활성 조건:

- 전송 시작: BLE 연결 완료 + 유효한 펌웨어 선택 + 전송 중이 아님
- 중단: 전송 중일 때만 활성
- 연결 해제/파일 변경: 전송 중에는 비활성

화면 회전 같은 configuration change에도 ViewModel의 연결/전송 상태와 로그가 유지되어야
한다. Activity가 종료될 때 불필요한 scan을 중지한다. 장시간 전송 중 화면이 꺼져 작업이
중단되지 않도록 화면 유지 또는 적절한 lifecycle 정책을 적용한다.

## 8. 상태와 오류 처리

권장 상태:

```text
Idle -> Scanning -> Connecting -> Discovering -> Connected
Connected -> Handshaking -> Uploading -> Completed
모든 진행 상태 -> Failed / Cancelled / Disconnected
```

다음 오류를 사용자에게 한국어로 구분해 표시한다.

- Bluetooth 미지원 또는 꺼짐
- BLE 권한 거부
- 검색 결과 없음
- 연결 실패/GATT status 오류
- NUS Service 또는 characteristic 없음
- Notify CCCD 활성화 실패
- 파일 형식/크기 오류
- 핸드셰이크 timeout
- ACK timeout/NAK/재시도 소진
- 사용자 중단
- 전송 중 연결 해제

## 9. BLE 모듈 전제 조건

BLE 모듈은 Peripheral/GATT Server이며 다음 설정을 사용한다.

```text
AT+ADVDATA=
AT+BAUD=19200
AT+TPMODE=1
AT+PIOCFG=0,0
AT+SCAN=0
```

`ADVDATA`는 `0x00` 한 byte가 아니라 길이 0의 빈 값이어야 한다. BLE 모듈과 MCU UART는
`19200 bps, 8-N-1`이며 TX/RX 교차 연결과 공통 GND가 필요하다.

## 10. 테스트 요구사항

최소한 다음 JVM unit test를 작성한다.

- `crc16("123456789") == 0x29B1`
- DATA 패킷 크기가 262 bytes인지 확인
- 패킷 length field가 `00 01`인지 확인
- 짧은 마지막 페이지가 `0xFF`로 padding되는지 확인
- CRC 범위와 little-endian 저장 확인
- END 패킷의 type, length, padding 확인
- 120 KiB 경계값 허용 및 초과 거부 확인

가능하면 BLE 계층을 interface로 분리하고 fake 구현으로 다음 uploader test도 작성한다.

- 핸드셰이크 성공
- NAK 후 재전송 성공
- ACK timeout 3회 후 실패
- 사용자 중단
- 연결 해제 시 업로드 취소

## 11. 완료 조건

- Gradle build가 성공한다.
- unit test가 모두 통과한다.
- Android 12 이상에서 BLE 권한 요청과 NUS 필터 검색이 동작한다.
- NUS 연결 후 notification 구독이 완료되어야 연결 완료로 표시된다.
- 기존 Web App과 동일한 바이트 패킷과 CRC를 생성한다.
- 실제 `LED_blink.bin` 파일을 선택해 수동 MCU 리셋 후 전송할 수 있다.
- 완료 시 모든 DATA 페이지와 END 패킷의 ACK를 받은 뒤 `펌웨어 전송 완료`를 표시한다.




