export const NUS_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_TX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';
export const NUS_RX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';

export class BleNusClient extends EventTarget {
  constructor() {
    super();
    this.device = null;
    this.tx = null;
    this.rx = null;
    this.onNotification = this.onNotification.bind(this);
    this.onDisconnected = this.onDisconnected.bind(this);
  }

  get connected() {
    return Boolean(this.device?.gatt?.connected && this.tx && this.rx);
  }

  async connect() {
    if (!navigator.bluetooth) throw new Error('이 브라우저는 Web Bluetooth를 지원하지 않습니다.');
    const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [NUS_SERVICE_UUID] }]
    });
    return this.connectDevice(device);
  }

  async reconnect() {
    if (!this.device) throw new Error('이 세션에 다시 연결할 장치가 없습니다.');
    return this.connectDevice(this.device);
  }

  async connectDevice(device) {
    this.device = device;
    this.device.removeEventListener('gattserverdisconnected', this.onDisconnected);
    const server = await this.device.gatt.connect();
    let service;
    try {
      service = await server.getPrimaryService(NUS_SERVICE_UUID);
    } catch (error) {
      if (this.device.gatt.connected) this.device.gatt.disconnect();
      this.clearConnection();
      throw new Error(
        `선택한 장치(${this.device.name || '이름 없음'})가 Nordic UART Service ` +
        `(${NUS_SERVICE_UUID})를 제공하지 않습니다. BLE 펌웨어의 서비스 UUID를 확인하세요.`
      );
    }
    this.tx = await service.getCharacteristic(NUS_TX_CHAR_UUID);
    this.rx = await service.getCharacteristic(NUS_RX_CHAR_UUID);
    await this.rx.startNotifications();
    this.rx.addEventListener('characteristicvaluechanged', this.onNotification);
    this.device.addEventListener('gattserverdisconnected', this.onDisconnected);
    return this.device;
  }

  async write(bytes) {
    if (!this.connected) throw new Error('BLE 장치가 연결되어 있지 않습니다.');
    if (typeof this.tx.writeValueWithoutResponse === 'function') {
      await this.tx.writeValueWithoutResponse(bytes);
    } else {
      await this.tx.writeValue(bytes);
    }
  }

  disconnect() {
    if (this.rx) this.rx.removeEventListener('characteristicvaluechanged', this.onNotification);
    if (this.device?.gatt?.connected) this.device.gatt.disconnect();
    this.clearConnection();
  }

  onNotification(event) {
    const view = event.target.value;
    const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
    this.dispatchEvent(new CustomEvent('data', { detail: new Uint8Array(bytes) }));
  }

  onDisconnected() {
    this.clearConnection();
    this.dispatchEvent(new Event('disconnected'));
  }

  clearConnection() {
    this.tx = null;
    this.rx = null;
  }
}
