"""
uart_uploader.py

hex 파일을 읽어서 UART로 MCU에 전송
사용법: python uart_uploader.py

필요한 라이브러리 설치:
  pip install pyserial
"""

import serial
import time

# ── 설정 ──────────────────────────────────
PORT      = 'COM4'          # 포트 번호
BAUD      = 9600            # 보드레이트
HEX_FILE  = 'LED_blink.hex'       # 전송할 hex 파일
PAGE_SIZE = 256             # Flash Page 크기
ACK       = 0x06            # MCU 응답 성공
END       = bytes([0xFF] * PAGE_SIZE)  # 종료 신호
# ──────────────────────────────────────────


# ── 1. Intel HEX → 바이너리 변환 ──
def hex_to_bin(path):
    """
    Intel HEX 파일을 읽어서
    연속된 바이너리 데이터로 변환
    """
    data = bytearray(0x1F800)   # App 영역 (Boot 이전까지)
    segment_base = 0
    max_addr = 0

    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue

            length   = int(line[1:3],  16)
            address  = int(line[3:7],  16)
            rec_type = int(line[7:9],  16)
            payload  = bytes.fromhex(line[9:9 + length * 2])

            if rec_type == 0x00:    # 데이터
                abs_addr = segment_base + address
                data[abs_addr:abs_addr + length] = payload
                max_addr = max(max_addr, abs_addr + length)

            elif rec_type == 0x01:  # EOF
                break

            elif rec_type == 0x02:  # 확장 세그먼트 주소
                segment_base = int.from_bytes(payload, 'big') * 0x10

    # 실제 데이터 크기로 트림
    return bytes(data[:max_addr])


# ── 2. 256바이트 단위로 자르기 ──
def split_pages(data):
    """
    바이너리를 256바이트 단위로 분할
    마지막 페이지는 0xFF로 패딩
    """
    pages = []
    for i in range(0, len(data), PAGE_SIZE):
        page = data[i:i + PAGE_SIZE]
        if len(page) < PAGE_SIZE:
            page = page + bytes([0xFF] * (PAGE_SIZE - len(page)))
        pages.append(page)
    return pages


# ── 3. 전송 ──
def upload(port, baud, hex_path):

    # hex → 바이너리 변환
    print(f"[1] hex 파일 변환 중: {hex_path}")
    try:
        binary = hex_to_bin(hex_path)
    except FileNotFoundError:
        print(f"    파일 없음: {hex_path}")
        return
    except Exception as e:
        print(f"    변환 오류: {e}")
        return

    pages = split_pages(binary)
    print(f"    총 {len(pages)} 페이지 ({len(binary)} bytes)")

    # 포트 연결
    print(f"[2] 포트 연결: {port} @ {baud}")
    try:
        ser = serial.Serial(port, baud, timeout=5)
    except Exception as e:
        print(f"    포트 오류: {e}")
        return
    time.sleep(0.5)

    # READY 수신 대기
    print("[3] MCU 준비 대기...")
    ready = ser.read(7)
    if b'READY' in ready:
        print("    READY 확인 ✅")
    else:
        print(f"    응답: {ready}")

    # 페이지 전송
    print("[4] 전송 시작")
    for i, page in enumerate(pages):

        retry = 0
        while retry < 3:

            # 256바이트 전송
            ser.write(page)

            # ACK 대기
            resp = ser.read(1)

            if not resp:
                print(f"    [{i+1}/{len(pages)}] 타임아웃 → 재시도 {retry+1}")
                retry += 1
                continue

            if resp[0] == ACK:
                print(f"    [{i+1}/{len(pages)}] ✅  주소: 0x{i*PAGE_SIZE:05X}")
                break
            else:
                print(f"    [{i+1}/{len(pages)}] 응답: 0x{resp[0]:02X} → 재시도 {retry+1}")
                retry += 1

        if retry >= 3:
            print("    최대 재시도 초과 → 중단")
            ser.close()
            return

    # 종료 신호 전송
    print("[5] 종료 신호 전송")
    ser.write(END)
    resp = ser.read(1)
    if resp and resp[0] == ACK:
        print("    완료 ✅ MCU가 종료 처리합니다")
    else:
        print(f"    종료 응답: {resp.hex() if resp else '없음'}")

    ser.close()
    print("[완료] 전송 종료")


# ── 진입점 ──
if __name__ == '__main__':
    upload(PORT, BAUD, HEX_FILE)
