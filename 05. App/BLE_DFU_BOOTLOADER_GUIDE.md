# BLE DFU Android 앱 기술 가이드

## 1. 문서 목적

이 문서는 Android 앱이 BLE를 통해 ATmega1281 부트로더로 `.bin` 펌웨어를 전송하는 전체 구조를 설명한다. 앱 사용법뿐 아니라 다음 내용을 이해할 수 있도록 작성했다.

- 부트로더와 Android 앱 사이의 데이터 흐름
- Nordic UART Service(NUS)와 Android BLE GATT API의 역할
- 핸드셰이크, 페이지 패킷, CRC, ACK/NAK 재시도 규약
- Kotlin Coroutine, StateFlow, ViewModel, Jetpack Compose의 연결 관계
- 파일 선택부터 전송 완료까지 각 소스 파일이 담당하는 기능
- 테스트, 빌드, 설치 및 장애 확인 방법

이 앱은 WebView로 웹 앱을 감싸지 않는다. Android의 BLE GATT API를 직접 사용하는 네이티브 앱이다.

## 2. 시스템 구성

```text
┌──────────────── Android 앱 ────────────────┐
│ Compose UI                                 │
│   ↓ 사용자 명령 / ↑ 진행 상태               │
│ DfuViewModel                               │
│   ↓ 펌웨어 데이터                          │
│ DfuUploader + DfuProtocol                  │
│   ↓ 20-byte BLE 청크 / ↑ R, ACK, NAK       │
│ BleNusClient                               │
└──────────────────┬─────────────────────────┘
                   │ BLE GATT / NUS
                   ▼
┌────────────── BLE 모듈 ────────────────────┐
│ NUS GATT Server ↔ UART transparent mode    │
└──────────────────┬─────────────────────────┘
                   │ UART 19200 bps, 8-N-1
                   ▼
┌──────────── ATmega1281 부트로더 ───────────┐
│ S/R 핸드셰이크                             │
│ 256-byte 페이지 수신 및 CRC 검사            │
│ Flash 기록 후 ACK 또는 NAK 반환             │
└────────────────────────────────────────────┘
```

Android에서 말하는 NUS의 Write characteristic은 BLE 모듈을 기준으로 수신(RX) 경로이며, 앱이 쓴 데이터는 BLE 모듈의 UART TX를 통해 MCU로 전달된다. 반대 방향의 MCU 응답은 BLE 모듈의 UART RX를 거쳐 NUS Notify characteristic으로 Android에 전달된다.

## 3. 프로젝트 구조와 책임

| 파일 | 책임 |
| --- | --- |
| `MainActivity.kt` | 런타임 권한, Bluetooth 활성화, 파일 선택 launcher, Compose 화면 시작 |
| `ble/BleModels.kt` | 검색 장치 모델, BLE 연결 상태, DFU용 BLE 추상 인터페이스 |
| `ble/BleNusClient.kt` | NUS 검색, GATT 연결, 서비스 검색, CCCD 설정, BLE 읽기/쓰기 |
| `dfu/DfuProtocol.kt` | 상수, CRC16, DATA/END 패킷 생성, 펌웨어 크기 검증 |
| `dfu/DfuUploader.kt` | 핸드셰이크, 페이지 전송, ACK/NAK/timeout 재시도 |
| `ui/DfuUiState.kt` | 화면에 표시할 불변 상태 모델 |
| `ui/DfuViewModel.kt` | BLE·파일·업로더를 연결하고 상태 및 로그 유지 |
| `ui/DfuScreen.kt` | BLE, 펌웨어, 진행률, 로그를 표시하는 Compose UI |
| `DfuProtocolTest.kt` | CRC 및 패킷 바이트 배열 검증 |
| `DfuUploaderTest.kt` | 성공, NAK 재시도, ACK timeout 동작 검증 |

의존 방향은 `UI → ViewModel → DFU/BLE`이다. 프로토콜 계층은 Compose나 Activity를 알지 못하며, BLE 계층도 화면을 직접 변경하지 않는다.

## 4. BLE 연결 과정

### 4.1 NUS UUID

| 역할 | UUID |
| --- | --- |
| NUS Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Android → MCU Write | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| MCU → Android Notify | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| CCCD | `00002902-0000-1000-8000-00805F9B34FB` |

### 4.2 검색

`BleNusClient.startScan()`은 다음 Android API를 사용한다.

- `BluetoothLeScanner.startScan()`
- `ScanFilter.Builder().setServiceUuid()`
- `ScanSettings.SCAN_MODE_LOW_LATENCY`
- `ScanCallback`

NUS Service UUID를 광고하는 장치만 검색한다. 결과는 MAC 주소를 키로 중복 제거하며 이름, 주소, RSSI를 `StateFlow<List<BleDeviceItem>>`로 화면에 전달한다. 검색은 사용자가 중지하거나 장치를 선택할 때까지 계속된다.

### 4.3 연결 완료 조건

앱은 `connectGatt()` 성공만으로 연결 완료를 표시하지 않는다. 다음 단계가 모두 성공해야 `Connected` 상태가 된다.

1. GATT 연결 성공
2. `discoverServices()` 성공
3. NUS Service 확인
4. Write/Notify characteristic 확인
5. `setCharacteristicNotification(true)` 성공
6. Notify characteristic의 CCCD에 `01 00` 기록 성공
7. `onDescriptorWrite()`에서 `GATT_SUCCESS` 확인

이 조건은 부트로더 응답을 놓치지 않기 위해 필요하다. CCCD 활성화 전에 `S`를 보내면 MCU의 `R` 응답이 Android에 전달되지 않을 수 있다.

모든 GATT callback은 status를 확인한다. 연결 실패, 서비스 부재, characteristic 부재, CCCD 실패가 발생하면 연결 대기와 DFU 동작이 실패 상태로 전환된다. 이전 연결의 늦은 callback은 현재 GATT 인스턴스와 비교하여 무시하므로 재연결 상태를 오염시키지 않는다.

## 5. Android 권한과 API 버전 차이

### Android 12(API 31) 이상

```xml
<uses-permission
    android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
```

### Android 11(API 30) 이하

```xml
<uses-permission android:name="android.permission.BLUETOOTH" android:maxSdkVersion="30" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" android:maxSdkVersion="30" />
<uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" android:maxSdkVersion="30" />
```

`MainActivity.requiredBlePermissions()`가 OS 버전에 따라 요청할 권한을 선택한다. `ActivityResultContracts.RequestMultiplePermissions`로 결과를 받고, Bluetooth가 꺼져 있으면 `BluetoothAdapter.ACTION_REQUEST_ENABLE`을 실행한다.

API 33 이상에서는 다음과 같이 값을 인자로 받는 새 GATT API를 사용한다.

- `writeCharacteristic(characteristic, value, writeType)`
- `writeDescriptor(descriptor, value)`
- `onCharacteristicChanged(gatt, characteristic, value)`

API 32 이하에서는 deprecated된 `characteristic.value`, `descriptor.value` 방식이 필요하므로 버전 분기가 존재한다.

## 6. 펌웨어 파일 처리

파일 선택은 Storage Access Framework의 `ActivityResultContracts.OpenDocument`를 사용한다. 앱이 저장소 전체 접근 권한을 요구하지 않고 사용자가 선택한 문서 URI만 읽는 방식이다.

검증 규칙은 다음과 같다.

- 파일명이 `.bin`으로 끝나야 한다.
- 빈 파일은 거부한다.
- 최대 크기는 122,880 bytes(120 KiB)이다.
- 읽는 도중 제한을 초과하면 즉시 중단한다.
- 전송 중에는 다른 파일 선택을 비활성화한다.

파일 읽기는 `Dispatchers.IO`에서 실행하므로 큰 파일을 읽어도 Compose 메인 스레드를 막지 않는다. 선택한 파일은 `ByteArray`로 ViewModel에 보관되고, UI에는 이름·크기·페이지 수만 노출한다.

페이지 수는 다음과 같다.

```text
pages = ceil(firmwareSize / 256)
```

## 7. 부트로더 프로토콜

### 7.1 주요 상수

| 이름 | 값 | 의미 |
| --- | ---: | --- |
| `START_CODE` | `0xEF` | 패킷 시작 |
| `MSG_DATA` | `0x01` | 펌웨어 페이지 패킷 |
| `MSG_END` | `0x02` | 전체 전송 종료 패킷 |
| `ACK` | `0x06` | 정상 수신 |
| `NAK` | `0x15` | 수신 또는 CRC 오류 |
| `HANDSHAKE_START` | `0x53` (`S`) | 앱의 부트로더 시작 요청 |
| `HANDSHAKE_READY` | `0x52` (`R`) | MCU 준비 완료 응답 |
| 페이지 크기 | 256 bytes | MCU Flash 기록 단위 |
| 패킷 크기 | 262 bytes | header + page + CRC |
| BLE 청크 크기 | 20 bytes | 한 번의 GATT write 크기 |
| 청크 간격 | 20 ms | BLE/UART 처리 여유 |
| ACK timeout | 5초 | 패킷 응답 제한 |
| 최대 전송 시도 | 3회 | 최초 시도 포함 |

### 7.2 CRC-16/CCITT-FALSE

```text
Initial value = 0xFFFF
Polynomial    = 0x1021
RefIn         = false
RefOut        = false
XorOut        = 0x0000
```

검증 문자열 `123456789`의 결과는 `0x29B1`이다.

CRC 계산 범위는 패킷 offset 1부터 259까지 총 259 bytes이다. 즉, `START_CODE`는 제외하고 다음 항목을 계산한다.

```text
message type(1) + length(2) + data field(256)
```

계산한 16-bit CRC는 little-endian으로 저장한다.

```text
packet[260] = CRC low byte
packet[261] = CRC high byte
```

### 7.3 262-byte 패킷 구조

| Offset | 크기 | 내용 |
| ---: | ---: | --- |
| 0 | 1 | `0xEF` |
| 1 | 1 | `MSG_DATA` 또는 `MSG_END` |
| 2 | 2 | 길이 256, little-endian `00 01` |
| 4 | 256 | 페이지 데이터 또는 `0xFF` padding |
| 260 | 2 | CRC16 little-endian |

마지막 DATA 페이지가 256 bytes보다 짧으면 남은 영역을 `0xFF`로 채운다. 이는 Flash의 지워진 기본값과 동일하다. length 필드는 실제 마지막 데이터 길이가 아니라 항상 256이다.

END 패킷도 동일한 262-byte 구조를 사용한다. type은 `0x02`, 데이터 256 bytes는 모두 `0xFF`이며 별도의 CRC를 계산한다. 따라서 마지막 DATA의 ACK를 받았다고 전체 전송이 끝난 것이 아니다. END 패킷의 ACK까지 받아야 완료다.

## 8. 전송 상태 머신

```text
Idle
  └─ BLE 검색 → Scanning → Connecting → Discovering → Connected
                                                        │
                                                        ▼
                                                  Handshaking
                                                        │ R
                                                        ▼
                                                   Uploading
                                                        │ END ACK
                                                        ▼
                                                   Completed

모든 진행 상태 → Failed / Cancelled / Disconnected
```

### 8.1 핸드셰이크

`DfuUploader.handshake()`의 순서는 다음과 같다.

1. 과거 Notify 응답을 큐에서 제거한다.
2. `S(0x53)` 한 바이트를 전송한다.
3. 최대 200 ms 동안 Notify 응답을 기다린다.
4. `R(0x52)`이면 페이지 전송을 시작한다.
5. 응답이 없으면 최대 150회 반복한다.

최대 시간은 약 30초다. 사용자는 이 시간 동안 MCU를 수동 리셋하여 부트로더가 `S`를 받을 수 있게 한다.

### 8.2 페이지 및 END 전송

각 262-byte 패킷은 최대 20-byte 청크로 나누어진다.

```text
262 bytes = 20 bytes × 13회 + 2 bytes × 1회
```

`BleNusClient.write()`는 다음 안전 조건을 적용한다.

- `Mutex`로 동시에 두 GATT write가 실행되지 않게 한다.
- Write Without Response를 사용한다.
- write API가 요청을 거부하면 즉시 실패한다.
- 각 write 뒤 20 ms 대기한다.
- 연결 상태가 아니면 쓰지 않는다.

패킷 전송 전에 과거 응답을 비우고 마지막 청크 전송 후 최대 5초간 ACK/NAK를 기다린다.

- ACK: 다음 페이지로 이동
- NAK: 동일 패킷 재전송
- timeout: 동일 패킷 재전송
- 3회 모두 실패: 전체 DFU 실패
- 알 수 없는 byte: hex 로그를 남기고 ACK/NAK 대기 계속

Notify payload에 여러 byte가 들어오면 `onCharacteristicChanged()`에서 byte 단위로 `Channel`에 넣는다. `DfuUploader`는 이 큐를 순서대로 소비한다. 이 구조는 한 번의 Notify에 `R`, `ACK` 또는 기타 상태 값이 같이 들어와도 순서를 보존한다.

## 9. Kotlin 비동기 기술의 역할

### Coroutine

BLE 연결, 파일 읽기, 핸드셰이크, ACK 대기는 UI 스레드를 막으면 안 된다. `suspend` 함수와 Coroutine으로 순차 프로토콜을 읽기 쉬운 형태로 표현한다.

- `viewModelScope`: ViewModel 수명에 연결된 작업 실행
- `Dispatchers.IO`: 펌웨어 파일 읽기
- `withTimeoutOrNull`: GATT 연결 및 응답 timeout
- `delay`: BLE 청크 간격과 경과 시간 갱신
- `Job.cancel()`: 사용자 전송 중단

### StateFlow

`BleNusClient`는 장치 목록과 연결 상태를 StateFlow로 제공한다. `DfuViewModel`은 이를 `DfuUiState`에 결합하고 Compose가 `collectAsState()`로 관찰한다.

상태 값이 바뀌면 화면을 직접 찾거나 수정하지 않아도 Compose가 필요한 UI를 다시 그린다.

### ViewModel

`DfuViewModel`은 다음 데이터를 Activity보다 오래 유지한다.

- BLE client와 현재 연결 상태
- 선택한 펌웨어 ByteArray
- DFU 진행률과 재시도 횟수
- 시작 시각과 로그
- 실행 중인 업로드 Job

화면 회전처럼 Activity가 재생성되어도 같은 ViewModel이 사용된다. ViewModel이 최종 제거될 때 `onCleared()`에서 scan을 중지하고 GATT를 닫는다.

주의: ViewModel은 Android 프로세스 종료 후에는 복원되지 않는다. 이 구현은 장시간 백그라운드 전송용 Foreground Service를 사용하지 않는다. 전송 중에는 `FLAG_KEEP_SCREEN_ON`으로 화면이 꺼지는 것을 방지하지만, 앱 프로세스가 강제로 종료되면 DFU도 중단된다.

## 10. Jetpack Compose UI 연결

`DfuScreen`은 네 영역으로 구성된다.

| 영역 | 표시 및 동작 |
| --- | --- |
| BLE 연결 | 상태, 선택 장치, 검색, 연결, 재연결, 해제, RSSI |
| 펌웨어 | 파일 선택, 파일명, 크기, 페이지 수 |
| DFU 전송 | 시작, 중단, 진행률, 페이지, 재시도, 경과 시간, 결과 |
| 로그 | millisecond timestamp 로그, 스크롤, 로그 지우기 |

버튼 활성 조건은 `DfuUiState`에서 계산한다.

```text
전송 시작 = BLE Connected + 유효한 펌웨어 + 전송 중 아님
중단       = Handshaking 또는 Uploading
연결 해제  = 전송 중이 아닐 때만 가능
파일 변경  = 전송 중이 아닐 때만 가능
```

UI는 BLE 객체나 패킷을 직접 다루지 않는다. 버튼 callback이 ViewModel 함수를 호출하고, 결과는 상태로 다시 화면에 전달된다.

## 11. 오류 처리와 복구

| 오류 | 앱 동작 |
| --- | --- |
| BLE 미지원 또는 Bluetooth 꺼짐 | 검색 중지 및 사용자 메시지 |
| 권한 거부 | Snackbar와 로그 표시 |
| 검색 실패 | Android scan error code 표시 |
| GATT 연결/검색 실패 | status와 함께 실패 처리 |
| NUS Service/characteristic 없음 | 연결 완료로 전환하지 않음 |
| CCCD 활성화 실패 | Notify 사용 불가이므로 연결 실패 |
| 잘못된 파일 또는 크기 초과 | 펌웨어 선택 거부 |
| 핸드셰이크 timeout | DFU 실패 |
| NAK/ACK timeout | 동일 패킷 최대 3회 시도 |
| 전송 중 연결 해제 | 다음 write 또는 ACK 처리에서 실패 |
| 사용자 중단 | Coroutine 취소 및 `Cancelled` 상태 |

BLE 연결이 끊어진 경우 진행 중이던 패킷의 중간 위치에서 재개하지 않는다. 재연결 후 펌웨어 전송을 처음부터 다시 시작해야 MCU와 Android의 페이지 상태가 일치한다.

## 12. BLE 모듈 및 MCU 조건

BLE 모듈은 GATT Server이자 UART 투명 전송 장치로 동작해야 한다.

```text
AT+ADVDATA=
AT+BAUD=19200
AT+TPMODE=1
AT+PIOCFG=0,0
AT+SCAN=0
```

`AT+ADVDATA=` 뒤에는 `0x00` 문자를 보내는 것이 아니라 길이 0의 빈 값을 설정해야 한다.

하드웨어 연결 조건:

- BLE 모듈 TX → MCU RX
- BLE 모듈 RX ← MCU TX
- 공통 GND
- UART `19200 bps, 8 data bits, no parity, 1 stop bit`
- BLE 광고 데이터에 NUS Service UUID 포함

광고에 NUS UUID가 없으면 모듈이 근처에 있어도 앱의 필터 검색 결과에 나타나지 않는다.

## 13. 테스트

### 프로토콜 테스트

`DfuProtocolTest`는 다음을 검증한다.

- `crc16("123456789") == 0x29B1`
- DATA 패킷 크기 262 bytes
- 시작 코드, type, length의 정확한 위치
- 마지막 페이지 `0xFF` padding
- full page 데이터 보존
- CRC 범위 및 little-endian 저장
- END 패킷 type, length, padding
- 120 KiB 허용 및 초과 거부

### 업로더 테스트

`DfuUploaderTest`는 실제 Bluetooth 대신 `DfuBleChannel` fake를 사용한다.

- `R` 핸드셰이크 후 DATA/END ACK 성공
- NAK 후 재전송 성공 및 재시도 수 증가
- ACK timeout 3회 후 실패

BLE 인터페이스를 분리했기 때문에 Android 기기 없이 JVM에서 부트로더 상태 머신을 검증할 수 있다.

실행 명령:

```powershell
$env:JAVA_HOME='C:\Program Files\Android\Android Studio\jbr'
.\gradlew.bat testDebugUnitTest assembleDebug
```

APK 출력:

```text
app/build/outputs/apk/debug/app-debug.apk
```

## 14. 설치와 실제 DFU 절차

### ADB 설치

```powershell
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" devices
& "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe" install -r `
  "app\build\outputs\apk\debug\app-debug.apk"
```

`devices` 결과가 `unauthorized`이면 Android 기기 화면에서 USB 디버깅을 허용해야 한다.

### DFU 실행

1. Android 기기에서 앱을 실행한다.
2. BLE 권한을 허용하고 Bluetooth를 켠다.
3. `BLE 검색`을 누른다.
4. NUS 장치의 이름·MAC 주소·RSSI를 확인하고 연결한다.
5. 상태가 `연결됨`이 될 때까지 기다린다.
6. `.bin 파일 선택`으로 최대 120 KiB 펌웨어를 고른다.
7. `전송 시작`을 누른다.
8. 핸드셰이크 중 MCU를 수동 리셋한다.
9. `부트로더 준비 응답 수신` 로그를 확인한다.
10. DATA 페이지 진행률과 ACK/NAK 재시도를 확인한다.
11. END ACK 뒤 `펌웨어 전송 완료`가 표시되는지 확인한다.

## 15. 보안 및 확장 시 고려사항

현재 프로토콜은 전송 오류 검출용 CRC만 제공한다. CRC는 펌웨어의 출처나 위변조를 인증하지 않는다. 제품 환경에서 보안 부팅이 필요하면 다음 항목을 MCU 부트로더와 함께 설계해야 한다.

- 펌웨어 디지털 서명 검증
- 버전 rollback 방지
- 장치별 인증 또는 암호화
- 전송 전 전체 이미지 hash와 크기 전달
- 실패 시 기존 애플리케이션 영역 보존

백그라운드에서도 안정적으로 전송해야 한다면 BLE 연결과 `DfuUploader`를 Foreground Service로 이동하고 notification으로 진행 상태를 표시하는 구조가 필요하다.

MTU를 확장하려면 Android와 BLE 모듈 양쪽이 지원하는지 먼저 확인해야 한다. 현재 구현은 기존 웹 앱 및 부트로더 호환성을 위해 20-byte 청크를 고정 사용한다.

## 16. 핵심 정리

이 앱에서 가장 중요한 완료 조건은 다음 세 가지다.

1. CCCD 활성화까지 성공해야 BLE 연결 완료다.
2. DATA 패킷은 256-byte 고정 페이지와 정확한 CRC를 사용하고 ACK를 받아야 한다.
3. 모든 DATA 뒤 END 패킷의 ACK까지 받아야 펌웨어 전송 완료다.

Compose와 ViewModel은 사용자 화면과 상태 유지를 담당하고, 실제 부트로더 호환성은 `DfuProtocol`, 전송 신뢰성은 `DfuUploader`, BLE 연결 안정성은 `BleNusClient`가 담당한다.
