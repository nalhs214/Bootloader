# BLE DFU Web App

Chrome의 Web Bluetooth API를 사용해 Nordic UART Service(NUS)를 제공하는 BLE 모듈에 연결하고,
ATmega1281 부트로더로 `.bin` 펌웨어를 전송하는 웹 애플리케이션입니다.

## 요구 사항

- Windows/macOS/Linux 데스크톱 Chrome 또는 Edge
- Web Bluetooth를 지원하는 환경
- NUS Peripheral/GATT Server로 동작하는 BLE 모듈
- Python 3 또는 VS Code Live Server
- 최대 120 KiB(122,880 bytes)의 `.bin` 펌웨어

## BLE 및 UART 설정

웹 앱에서 사용하는 NUS UUID는 다음과 같습니다.

| 용도 | UUID |
| --- | --- |
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| PC -> MCU Write | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| MCU -> PC Notify | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

BLE 모듈과 ATmega1281 사이의 UART는 `19200 bps, 8-N-1`로 설정합니다.
BLE 연결을 해제한 상태에서 모듈에 다음 AT 명령을 입력하세요.

```text
AT+ADVDATA=
AT+BAUD=19200
AT+TPMODE=1
AT+PIOCFG=0,0
AT+SCAN=0
```

`ADVDATA`는 `0x00`이 아니라 길이 0의 빈 값이어야 합니다. Manufacturer Data가 설정되면
legacy advertising 패킷에서 NUS Service UUID가 빠져 웹 앱 검색에 표시되지 않을 수 있습니다.

## 실행 방법

이 디렉터리에서 로컬 웹 서버를 실행합니다.

```powershell
python -m http.server 8000
```

브라우저에서 다음 주소를 엽니다.

```text
http://localhost:8000
```

서버를 종료하려면 터미널에서 `Ctrl+C`를 누릅니다. VS Code를 사용한다면 `Go Live`로도
실행할 수 있습니다. Web Bluetooth는 `file://` 페이지에서 실행하지 말고 `localhost` 또는
HTTPS에서 사용해야 합니다.

## 펌웨어 전송

1. `BLE 연결`을 눌러 NUS 장치를 선택합니다.
2. 펌웨어 파일 영역을 클릭하거나 `.bin` 파일을 드래그 앤 드롭합니다.
3. `전송 시작`을 누릅니다.
4. 핸드셰이크 `S(0x53)`가 전송되는 동안 ATmega1281을 수동 리셋합니다.
5. 부트로더의 `R(0x52)` 응답 후 페이지 전송이 완료될 때까지 기다립니다.

각 데이터 페이지는 256 bytes이며, 패킷은 BLE에서 20-byte 청크로 나누어 전송됩니다.
연결이 끊기면 이어 보내지 않고 다음 전송을 처음부터 다시 시작합니다.

## 문제 해결

### BLE 장치가 검색되지 않음

- `AT+ADVDATA`가 빈 값인지 확인합니다.
- `AT+SCAN=0`으로 BLE 모듈의 Central 스캔을 중지합니다.
- 펌웨어가 NUS UUID로 빌드되었는지 확인합니다.
- nRF Connect에서 advertising 패킷에 NUS Service UUID가 표시되는지 확인합니다.

### Nordic UART Service를 찾지 못함

- nRF Connect에서 실제 GATT Service 목록을 확인합니다.
- 이전에 같은 MAC 주소로 다른 GATT 구성을 사용했다면 Windows Bluetooth 장치와 Chrome의
  장치 권한을 제거한 뒤 Chrome을 다시 시작합니다.

### 핸드셰이크 응답이 없음

- `AT+BAUD`가 `19200`인지 확인합니다.
- `AT+TPMODE`가 `1`인지 확인합니다.
- MODE/TRAN 핀 기능을 사용한다면 핀이 Throughput Mode 상태인지 확인합니다.
- BLE 모듈 TX/RX와 MCU RX/TX가 교차 연결되고 GND가 공통인지 확인합니다.
- 전송 시작 후 30초 이내에 MCU를 리셋하여 부트로더 대기 상태로 진입시킵니다.

## 파일 구성

| 파일 | 역할 |
| --- | --- |
| `index.html` | 메인 UI로 이동하는 진입 페이지 |
| `ble_dfu_ui_white_green.html` | 화면 마크업과 스타일 |
| `ble.js` | Web Bluetooth 연결과 NUS 송수신 |
| `protocol.js` | 패킷 생성, CRC16, 청크 전송 |
| `main.js` | UI, 핸드셰이크, 펌웨어 전송 흐름 |
