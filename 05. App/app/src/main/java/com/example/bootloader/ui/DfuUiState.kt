package com.example.bootloader.ui

import com.example.bootloader.ble.BleConnectionState
import com.example.bootloader.ble.BleDeviceItem

enum class DfuStage { Idle, Handshaking, Uploading, Completed, Failed, Cancelled }

data class FirmwareInfo(
    val name: String,
    val size: Int,
    val pages: Int,
)

data class DfuUiState(
    val bleState: BleConnectionState = BleConnectionState.Idle,
    val devices: List<BleDeviceItem> = emptyList(),
    val selectedDevice: BleDeviceItem? = null,
    val firmware: FirmwareInfo? = null,
    val loadingFile: Boolean = false,
    val stage: DfuStage = DfuStage.Idle,
    val completedPages: Int = 0,
    val totalPages: Int = 0,
    val retryCount: Int = 0,
    val elapsedMillis: Long = 0,
    val logs: List<String> = emptyList(),
    val message: String? = null,
) {
    val uploading: Boolean get() = stage == DfuStage.Handshaking || stage == DfuStage.Uploading
    val canStart: Boolean get() = bleState == BleConnectionState.Connected && firmware != null && !uploading
}
