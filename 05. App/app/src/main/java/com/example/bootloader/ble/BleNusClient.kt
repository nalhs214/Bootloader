package com.example.bootloader.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.util.UUID

@SuppressLint("MissingPermission")
class BleNusClient(context: Context) : DfuBleChannel {
    companion object {
        val SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val WRITE_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val NOTIFY_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")
    }

    private val appContext = context.applicationContext
    private val adapter: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter
    private val responseChannel = Channel<Byte>(Channel.UNLIMITED)
    private val writeMutex = Mutex()
    private val foundByAddress = linkedMapOf<String, BleDeviceItem>()
    private var gatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    private var connectResult: CompletableDeferred<Result<Unit>>? = null

    private val _devices = MutableStateFlow<List<BleDeviceItem>>(emptyList())
    val devices = _devices.asStateFlow()
    private val _state = MutableStateFlow(BleConnectionState.Idle)
    val state = _state.asStateFlow()
    private val _errors = MutableSharedFlow<String>(extraBufferCapacity = 8)
    val errors = _errors.asSharedFlow()

    val bluetoothAvailable: Boolean get() = adapter != null
    val bluetoothEnabled: Boolean get() = adapter?.isEnabled == true
    override val connected: Boolean get() = _state.value == BleConnectionState.Connected

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) = addResult(result)
        override fun onBatchScanResults(results: MutableList<ScanResult>) = results.forEach(::addResult)
        override fun onScanFailed(errorCode: Int) {
            _state.value = BleConnectionState.Failed
            _errors.tryEmit("BLE 검색에 실패했습니다. 오류 코드: $errorCode")
        }
    }

    fun startScan() {
        val scanner = adapter?.bluetoothLeScanner
        if (adapter == null) {
            _errors.tryEmit("이 기기는 BLE를 지원하지 않습니다.")
            return
        }
        if (!adapter.isEnabled || scanner == null) {
            _errors.tryEmit("Bluetooth가 꺼져 있습니다.")
            return
        }
        stopScan()
        foundByAddress.clear()
        _devices.value = emptyList()
        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(listOf(filter), settings, scanCallback)
        _state.value = BleConnectionState.Scanning
    }

    fun stopScan() {
        runCatching { adapter?.bluetoothLeScanner?.stopScan(scanCallback) }
        if (_state.value == BleConnectionState.Scanning) _state.value = BleConnectionState.Idle
    }

    suspend fun connect(address: String): Result<Unit> {
        stopScan()
        disconnect()
        val device = runCatching { adapter?.getRemoteDevice(address) }.getOrNull()
            ?: return Result.failure(IllegalStateException("BLE 장치를 찾을 수 없습니다."))
        _state.value = BleConnectionState.Connecting
        val deferred = CompletableDeferred<Result<Unit>>()
        connectResult = deferred
        gatt = device.connectGatt(appContext, false, gattCallback, BluetoothDeviceTransport.LE)
        val result = withTimeoutOrNull(15_000) { deferred.await() }
        if (result != null) return result
        disconnect()
        return Result.failure<Unit>(IllegalStateException("BLE 연결 시간이 초과되었습니다."))
    }

    fun disconnect() {
        connectResult?.takeIf { !it.isCompleted }
            ?.complete(Result.failure(IllegalStateException("BLE 연결이 취소되었습니다.")))
        connectResult = null
        writeCharacteristic = null
        gatt?.let { current -> runCatching { current.disconnect() }; current.close() }
        gatt = null
        if (_state.value != BleConnectionState.Idle) _state.value = BleConnectionState.Disconnected
    }

    override suspend fun write(value: ByteArray) = writeMutex.withLock {
        check(connected) { "BLE가 연결되어 있지 않습니다." }
        require(value.size <= 20) { "BLE 청크는 20바이트를 초과할 수 없습니다." }
        val currentGatt = checkNotNull(gatt)
        val characteristic = checkNotNull(writeCharacteristic)
        val accepted = if (Build.VERSION.SDK_INT >= 33) {
            currentGatt.writeCharacteristic(
                characteristic,
                value,
                BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE,
            ) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
            @Suppress("DEPRECATION")
            characteristic.value = value
            @Suppress("DEPRECATION")
            currentGatt.writeCharacteristic(characteristic)
        }
        check(accepted) { "GATT 쓰기 요청이 거부되었습니다." }
        delay(20)
    }

    override suspend fun receive(timeoutMs: Long): Byte? =
        withTimeoutOrNull(timeoutMs) { responseChannel.receive() }

    override fun drainResponses() {
        while (responseChannel.tryReceive().isSuccess) Unit
    }

    fun close() {
        stopScan()
        disconnect()
        responseChannel.close()
    }

    private fun addResult(result: ScanResult) {
        val address = result.device.address
        val name = result.scanRecord?.deviceName ?: runCatching { result.device.name }.getOrNull()
            ?: "이름 없는 장치"
        foundByAddress[address] = BleDeviceItem(name, address, result.rssi)
        _devices.value = foundByAddress.values.sortedByDescending { it.rssi }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (this@BleNusClient.gatt !== gatt) {
                gatt.close()
                return
            }
            if (status == BluetoothGatt.GATT_SUCCESS && newState == BluetoothProfile.STATE_CONNECTED) {
                _state.value = BleConnectionState.Discovering
                if (!gatt.discoverServices()) fail("서비스 검색 요청이 거부되었습니다.")
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                val message = if (status == BluetoothGatt.GATT_SUCCESS) "BLE 연결이 해제되었습니다."
                    else "BLE 연결 오류 (GATT status $status)"
                fail(message, disconnected = true)
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (this@BleNusClient.gatt !== gatt) return
            if (status != BluetoothGatt.GATT_SUCCESS) return fail("서비스 검색 실패 (status $status)")
            val service = gatt.getService(SERVICE_UUID) ?: return fail("NUS 서비스를 찾을 수 없습니다.")
            val write = service.getCharacteristic(WRITE_UUID)
                ?: return fail("NUS Write characteristic을 찾을 수 없습니다.")
            val notify = service.getCharacteristic(NOTIFY_UUID)
                ?: return fail("NUS Notify characteristic을 찾을 수 없습니다.")
            val descriptor = notify.getDescriptor(CCCD_UUID)
                ?: return fail("Notify CCCD를 찾을 수 없습니다.")
            writeCharacteristic = write
            if (!gatt.setCharacteristicNotification(notify, true)) return fail("Notify 활성화에 실패했습니다.")
            val accepted = if (Build.VERSION.SDK_INT >= 33) {
                gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) ==
                    BluetoothStatusCodes.SUCCESS
            } else {
                @Suppress("DEPRECATION")
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                @Suppress("DEPRECATION")
                gatt.writeDescriptor(descriptor)
            }
            if (!accepted) fail("CCCD 쓰기 요청이 거부되었습니다.")
        }

        override fun onDescriptorWrite(gatt: BluetoothGatt, descriptor: BluetoothGattDescriptor, status: Int) {
            if (this@BleNusClient.gatt !== gatt) return
            if (descriptor.uuid != CCCD_UUID) return
            if (status == BluetoothGatt.GATT_SUCCESS) {
                _state.value = BleConnectionState.Connected
                connectResult?.complete(Result.success(Unit))
            } else fail("Notify CCCD 활성화 실패 (status $status)")
        }

        @Deprecated("Deprecated in API 33")
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (this@BleNusClient.gatt !== gatt) return
            characteristic.value?.forEach(responseChannel::trySend)
        }

        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray,
        ) {
            if (this@BleNusClient.gatt !== gatt) return
            value.forEach(responseChannel::trySend)
        }
    }

    private fun fail(message: String, disconnected: Boolean = false) {
        _errors.tryEmit(message)
        _state.value = if (disconnected) BleConnectionState.Disconnected else BleConnectionState.Failed
        connectResult?.takeIf { !it.isCompleted }?.complete(Result.failure(IllegalStateException(message)))
        if (!disconnected) {
            writeCharacteristic = null
            gatt?.let { runCatching { it.disconnect() }; it.close() }
            gatt = null
        }
    }
}

private object BluetoothDeviceTransport {
    const val LE = 2
}
