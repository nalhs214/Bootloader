import { BleNusClient } from './ble.js';
import {
  ACK, NAK, MSG_DATA, MSG_END, PAGE_SIZE, MAX_RETRY, ACK_TIMEOUT,
  MAX_FIRMWARE_SIZE, makePacket, sendChunked, packetPreview
} from './protocol.js';

const ids = [
  'connectionStatus', 'connectionTag', 'deviceName', 'firmwareFile', 'fileZone',
  'fileName', 'fileMeta', 'connectButton', 'reconnectButton', 'disconnectButton',
  'startButton', 'cancelButton', 'progress', 'progressPages', 'progressText',
  'progressPercent', 'sentPages', 'retryCount', 'elapsedTime', 'log',
  'clearLogButton', 'stepConnect', 'stepFile', 'stepTransfer', 'stepComplete',
  'lineConnect', 'lineFile', 'lineTransfer'
];
const ui = Object.fromEntries(ids.map(id => [id, document.getElementById(id)]));
const ble = new BleNusClient();

let firmware = null;
let transferring = false;
let cancelled = false;
let pendingResponse = null;
let retries = 0;
let startedAt = 0;
let elapsedTimer = null;
let completed = false;

function log(message, type = 'info') {
  const row = document.createElement('div');
  row.className = 'log-line';
  const time = document.createElement('span');
  time.className = 'log-time';
  time.textContent = new Date().toLocaleTimeString('ko-KR', { hour12: false });
  const text = document.createElement('span');
  text.className = `log-${type}`;
  text.textContent = message;
  row.append(time, text);
  ui.log.append(row);
  ui.log.scrollTop = ui.log.scrollHeight;
}

function setStep(element, number, state) {
  element.classList.toggle('step-done', state === 'done');
  element.classList.toggle('step-active', state === 'active');
  element.querySelector('.step-num').textContent = state === 'done' ? '✓' : number;
}

function updateSteps() {
  const connected = ble.connected;
  setStep(ui.stepConnect, 1, connected ? 'done' : 'active');
  setStep(ui.stepFile, 2, firmware ? 'done' : connected ? 'active' : 'idle');
  setStep(ui.stepTransfer, 3, completed ? 'done' : transferring ? 'active' : 'idle');
  setStep(ui.stepComplete, 4, completed ? 'done' : 'idle');
  ui.lineConnect.classList.toggle('step-line-done', connected);
  ui.lineFile.classList.toggle('step-line-done', Boolean(firmware));
  ui.lineTransfer.classList.toggle('step-line-done', completed);
}

function updateControls() {
  ui.connectButton.disabled = transferring || ble.connected;
  ui.reconnectButton.disabled = transferring || ble.connected || !ble.device;
  ui.disconnectButton.disabled = transferring || !ble.connected;
  ui.startButton.disabled = transferring || !ble.connected || !firmware;
  ui.cancelButton.disabled = !transferring;
  ui.firmwareFile.disabled = transferring;
  ui.fileZone.classList.toggle('file-zone-disabled', transferring);
  ui.fileZone.tabIndex = transferring ? -1 : 0;
  updateSteps();
}

async function selectFirmwareFile(file) {
  if (!file?.size) {
    firmware = null;
    log('비어 있거나 유효하지 않은 파일입니다.', 'err');
  } else if (!file.name.toLowerCase().endsWith('.bin')) {
    firmware = null;
    ui.fileName.textContent = file.name;
    ui.fileMeta.textContent = 'BIN 파일만 사용할 수 있습니다.';
    log('BIN 형식의 펌웨어 파일을 선택하세요.', 'err');
  } else if (file.size > MAX_FIRMWARE_SIZE) {
    firmware = null;
    ui.fileName.textContent = file.name;
    ui.fileMeta.textContent = `${file.size.toLocaleString()} bytes · 최대 122,880 bytes 초과`;
    log('펌웨어가 애플리케이션 영역 120 KiB를 초과합니다.', 'err');
  } else {
    firmware = new Uint8Array(await file.arrayBuffer());
    const pages = Math.ceil(file.size / PAGE_SIZE);
    ui.fileName.textContent = file.name;
    ui.fileMeta.textContent = `${file.size.toLocaleString()} bytes · ${pages} pages · 256 B/page`;
    completed = false;
    setProgress(0, pages, '전송 대기');
    log(`파일 선택: ${file.name} (${file.size.toLocaleString()} bytes)`, 'info');
  }
  updateControls();
}

function setConnection(connected, name = '') {
  const deviceName = name || ble.device?.name || '이름 없는 BLE 장치';
  ui.connectionStatus.lastChild.textContent = connected ? ` ${deviceName} 연결됨` : ' 연결 안 됨';
  ui.connectionStatus.style.color = connected ? '#16a34a' : '#6b7280';
  ui.connectionStatus.querySelector('.dot').style.background = connected ? '#16a34a' : '#9ca3af';
  ui.deviceName.textContent = ble.device?.name || '연결 기록 없음';
  ui.connectionTag.textContent = connected ? '연결됨' : '연결 안 됨';
  ui.connectionTag.className = `tag ${connected ? 'tag-green' : 'tag-gray'}`;
  ui.connectionTag.style.marginLeft = 'auto';
  updateControls();
}

function setProgress(done, total, text) {
  const percent = total ? Math.round((done / total) * 100) : 0;
  ui.progress.style.width = `${percent}%`;
  ui.progressPages.textContent = `${done} / ${total} pages`;
  ui.progressText.textContent = text;
  ui.progressPercent.textContent = `${percent}%`;
  ui.sentPages.textContent = done;
}

function startElapsedTimer() {
  startedAt = performance.now();
  clearInterval(elapsedTimer);
  const render = () => { ui.elapsedTime.textContent = `${((performance.now() - startedAt) / 1000).toFixed(1)}s`; };
  render();
  elapsedTimer = setInterval(render, 100);
}

function armResponse(acceptedBytes, timeout) {
  pendingResponse?.resolve(null);
  let timer;
  return new Promise(resolve => {
    pendingResponse = {
      acceptedBytes,
      resolve: value => {
        clearTimeout(timer);
        pendingResponse = null;
        resolve(value);
      }
    };
    timer = setTimeout(() => pendingResponse?.resolve(null), timeout);
  });
}

ble.addEventListener('data', event => {
  for (const byte of event.detail) {
    const hex = byte.toString(16).padStart(2, '0').toUpperCase();
    if (byte === ACK) log(`ACK 수신 (${hex})`, 'ok');
    else if (byte === NAK) log(`NAK 수신 (${hex})`, 'err');
    else if (byte === 0x52) log(`READY 수신 (${hex})`, 'ok');
    else log(`RX ${hex}`, 'hex');
    if (pendingResponse?.acceptedBytes.includes(byte)) {
      pendingResponse.resolve(byte);
      break;
    }
  }
});

ble.addEventListener('disconnected', () => {
  pendingResponse?.resolve(null);
  transferring = false;
  cancelled = true;
  clearInterval(elapsedTimer);
  setConnection(false);
  log('BLE 연결이 해제되었습니다. 다음 전송은 처음부터 시작합니다.', 'warn');
});

async function handshake() {
  for (let attempt = 1; attempt <= 150; attempt += 1) {
    if (cancelled) throw new DOMException('전송이 중단되었습니다.', 'AbortError');
    const response = armResponse([0x52], 200);
    await ble.write(new Uint8Array([0x53]));
    if (attempt === 1 || attempt % 25 === 0) {
      log(`핸드셰이크 S 전송 성공 (${attempt}/150)`, 'info');
    }
    if (await response === 0x52) return;
  }
  throw new Error(
    'S(0x53)를 150회 전송했지만 30초 안에 부트로더 응답 R(0x52)을 받지 못했습니다. ' +
    'MCU 리셋, UART 19200 bps, Throughput Mode를 확인하세요.'
  );
}

async function sendWithAck(packet, label) {
  for (let attempt = 1; attempt <= MAX_RETRY; attempt += 1) {
    if (attempt > 1) {
      retries += 1;
      ui.retryCount.textContent = retries;
      log(`${label} 재시도 ${attempt}/${MAX_RETRY}`, 'warn');
    }
    const response = armResponse([ACK, NAK], ACK_TIMEOUT);
    log(`[${label}] ${packetPreview(packet)}`, 'hex');
    await sendChunked(bytes => ble.write(bytes), packet, () => cancelled);
    const result = await response;
    if (result === ACK) return;
    log(result === NAK ? `${label} NAK` : `${label} ACK 시간 초과`, 'warn');
  }
  throw new Error(`${label} 전송이 ${MAX_RETRY}회 실패했습니다.`);
}

async function upload() {
  transferring = true;
  cancelled = false;
  completed = false;
  retries = 0;
  ui.retryCount.textContent = '0';
  updateControls();
  startElapsedTimer();
  const pageCount = Math.ceil(firmware.byteLength / PAGE_SIZE);
  try {
    setProgress(0, pageCount, '핸드셰이크 중...');
    log('핸드셰이크 전송: 53', 'info');
    await handshake();
    for (let index = 0; index < pageCount; index += 1) {
      const page = firmware.slice(index * PAGE_SIZE, (index + 1) * PAGE_SIZE);
      setProgress(index, pageCount, '전송 중...');
      await sendWithAck(makePacket(MSG_DATA, page), `PAGE ${index + 1}/${pageCount}`);
      setProgress(index + 1, pageCount, '전송 중...');
    }
    await sendWithAck(makePacket(MSG_END), 'END');
    completed = true;
    setProgress(pageCount, pageCount, '전송 완료');
    log('펌웨어 전송 완료', 'ok');
  } catch (error) {
    const aborted = error.name === 'AbortError';
    log(aborted ? '사용자가 전송을 중단했습니다.' : error.message, aborted ? 'warn' : 'err');
    ui.progressText.textContent = aborted ? '중단됨' : '전송 실패';
  } finally {
    pendingResponse?.resolve(null);
    transferring = false;
    clearInterval(elapsedTimer);
    updateControls();
  }
}

async function connect(reconnect = false) {
  try {
    log(reconnect ? '마지막 장치 재연결 시도...' : 'BLE 장치 검색...', 'info');
    const device = reconnect ? await ble.reconnect() : await ble.connect();
    setConnection(true, device.name);
    log(`연결 성공: ${device.name || '이름 없는 장치'}`, 'ok');
  } catch (error) {
    if (error.name !== 'NotFoundError') log(error.message, 'err');
    setConnection(false);
  }
}

ui.connectButton.addEventListener('click', () => connect(false));
ui.reconnectButton.addEventListener('click', () => connect(true));
ui.disconnectButton.addEventListener('click', () => {
  ble.disconnect();
  setConnection(false);
  log('BLE 연결을 해제했습니다.', 'info');
});
ui.fileZone.addEventListener('click', event => {
  if (!transferring && event.target !== ui.firmwareFile) ui.firmwareFile.click();
});
ui.fileZone.addEventListener('keydown', event => {
  if (!transferring && (event.key === 'Enter' || event.key === ' ')) {
    event.preventDefault();
    ui.firmwareFile.click();
  }
});
ui.fileZone.addEventListener('dragover', event => {
  event.preventDefault();
  if (!transferring) {
    event.dataTransfer.dropEffect = 'copy';
    ui.fileZone.classList.add('drag-over');
  }
});
ui.fileZone.addEventListener('dragleave', event => {
  if (!ui.fileZone.contains(event.relatedTarget)) ui.fileZone.classList.remove('drag-over');
});
ui.fileZone.addEventListener('drop', event => {
  event.preventDefault();
  ui.fileZone.classList.remove('drag-over');
  if (!transferring) selectFirmwareFile(event.dataTransfer.files[0]);
});
ui.firmwareFile.addEventListener('change', event => {
  selectFirmwareFile(event.target.files[0]);
});
ui.startButton.addEventListener('click', upload);
ui.cancelButton.addEventListener('click', () => {
  cancelled = true;
  pendingResponse?.resolve(null);
});
ui.clearLogButton.addEventListener('click', () => { ui.log.replaceChildren(); });
setConnection(false);
