export const START_CODE = 0xEF;
export const MSG_DATA = 0x01;
export const MSG_END = 0x02;
export const ACK = 0x06;
export const NAK = 0x15;
export const PAGE_SIZE = 256;
export const PKT_SIZE = 262;
export const CHUNK_SIZE = 20;
export const CHUNK_DELAY = 20;
export const MAX_RETRY = 3;
export const ACK_TIMEOUT = 5000;
export const MAX_FIRMWARE_SIZE = 120 * 1024;

export function crc16(data) {
  let crc = 0xFFFF;
  for (const byte of data) {
    crc ^= byte << 8;
    for (let bit = 0; bit < 8; bit += 1) {
      crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
      crc &= 0xFFFF;
    }
  }
  return crc;
}

export function makePacket(msgType, pageData = null) {
  if (msgType !== MSG_DATA && msgType !== MSG_END) throw new RangeError('지원하지 않는 메시지 유형입니다.');
  if (pageData && pageData.length > PAGE_SIZE) throw new RangeError('페이지 데이터는 256바이트 이하여야 합니다.');

  const packet = new Uint8Array(PKT_SIZE);
  packet[0] = START_CODE;
  packet[1] = msgType;
  // The bootloader expects the padded 256-byte field for DATA and END packets.
  const dataLength = PAGE_SIZE;
  packet[2] = dataLength & 0xFF;
  packet[3] = dataLength >> 8;
  packet.fill(0xFF, 4, 260);
  if (pageData?.length) packet.set(pageData, 4);

  const crc = crc16(packet.subarray(1, 260));
  packet[260] = crc & 0xFF;
  packet[261] = crc >> 8;
  return packet;
}

export const sleep = (milliseconds) => new Promise(resolve => setTimeout(resolve, milliseconds));

export async function sendChunked(write, packet, shouldCancel = () => false) {
  for (let offset = 0; offset < packet.length; offset += CHUNK_SIZE) {
    if (shouldCancel()) throw new DOMException('전송이 중단되었습니다.', 'AbortError');
    await write(packet.slice(offset, offset + CHUNK_SIZE));
    await sleep(CHUNK_DELAY);
  }
}

export function packetPreview(packet) {
  const hex = bytes => Array.from(bytes, byte => byte.toString(16).padStart(2, '0').toUpperCase()).join(' ');
  return `${hex(packet.slice(0, 10))} ... ${hex(packet.slice(258))}`;
}
