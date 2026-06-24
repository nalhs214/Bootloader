package com.example.bootloader.ble

data class BleDeviceItem(
    val name: String,
    val address: String,
    val rssi: Int,
)

enum class BleConnectionState {
    Idle, Scanning, Connecting, Discovering, Connected, Disconnected, Failed
}

interface DfuBleChannel {
    val connected: Boolean
    suspend fun write(value: ByteArray)
    suspend fun receive(timeoutMs: Long): Byte?
    fun drainResponses()
}
