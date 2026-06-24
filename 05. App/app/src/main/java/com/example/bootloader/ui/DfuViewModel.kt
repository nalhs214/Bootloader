package com.example.bootloader.ui

import android.app.Application
import android.net.Uri
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.bootloader.ble.BleConnectionState
import com.example.bootloader.ble.BleDeviceItem
import com.example.bootloader.ble.BleNusClient
import com.example.bootloader.dfu.DfuProtocol
import com.example.bootloader.dfu.DfuUploader
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.ceil

class DfuViewModel(application: Application) : AndroidViewModel(application) {
    private val ble = BleNusClient(application)
    private val _uiState = MutableStateFlow(DfuUiState())
    val uiState = _uiState.asStateFlow()
    private var firmwareBytes: ByteArray? = null
    private var uploadJob: Job? = null
    private var startedAt = 0L

    init {
        viewModelScope.launch {
            ble.devices.collect { devices -> _uiState.update { it.copy(devices = devices) } }
        }
        viewModelScope.launch {
            ble.state.collect { state -> _uiState.update { it.copy(bleState = state) } }
        }
        viewModelScope.launch {
            ble.errors.collect { error -> showMessage(error); log(error) }
        }
    }

    val bluetoothAvailable: Boolean get() = ble.bluetoothAvailable
    val bluetoothEnabled: Boolean get() = ble.bluetoothEnabled

    fun startScan() {
        if (_uiState.value.uploading) return
        ble.startScan()
        log("NUS 장치 검색을 시작했습니다.")
    }

    fun stopScan() = ble.stopScan()

    fun connect(device: BleDeviceItem) {
        if (_uiState.value.uploading) return
        _uiState.update { it.copy(selectedDevice = device) }
        viewModelScope.launch {
            log("${device.name} 연결 중...")
            ble.connect(device.address)
                .onSuccess { log("${device.name} 연결 완료 (NUS Notify 활성화)") }
                .onFailure { showMessage(it.message ?: "BLE 연결에 실패했습니다.") }
        }
    }

    fun reconnect() {
        _uiState.value.selectedDevice?.let(::connect)
    }

    fun disconnect() {
        if (_uiState.value.uploading) return
        ble.disconnect()
        log("BLE 연결을 해제했습니다.")
    }

    fun selectFirmware(uri: Uri) {
        if (_uiState.value.uploading) return
        _uiState.update { it.copy(loadingFile = true, message = null) }
        viewModelScope.launch {
            runCatching {
                val (name, bytes) = readFirmware(uri)
                if (!name.lowercase(Locale.ROOT).endsWith(".bin")) {
                    error(".bin 펌웨어 파일만 선택할 수 있습니다.")
                }
                DfuProtocol.validateFirmware(bytes)?.let(::error)
                name to bytes
            }
                .onSuccess { (name, bytes) ->
                    firmwareBytes = bytes
                    val pages = ceil(bytes.size / DfuProtocol.PAGE_SIZE.toDouble()).toInt()
                    _uiState.update {
                        it.copy(
                            firmware = FirmwareInfo(name, bytes.size, pages),
                            loadingFile = false,
                            totalPages = pages,
                            completedPages = 0,
                        )
                    }
                    log("펌웨어 선택: $name (${bytes.size} bytes, $pages 페이지)")
                }
                .onFailure { error ->
                    firmwareBytes = null
                    _uiState.update { it.copy(firmware = null, loadingFile = false) }
                    showMessage(error.message ?: "펌웨어 파일을 읽을 수 없습니다.")
                }
        }
    }

    fun startUpload() {
        val bytes = firmwareBytes ?: return
        if (!_uiState.value.canStart) return
        startedAt = System.currentTimeMillis()
        _uiState.update {
            it.copy(stage = DfuStage.Handshaking, completedPages = 0, retryCount = 0, elapsedMillis = 0)
        }
        uploadJob = viewModelScope.launch {
            val ticker = launch {
                while (true) {
                    delay(250)
                    _uiState.update { it.copy(elapsedMillis = System.currentTimeMillis() - startedAt) }
                }
            }
            runCatching {
                DfuUploader(
                    channel = ble,
                    onLog = ::log,
                    onProgress = { completed, total, retries ->
                        _uiState.update {
                            it.copy(
                                stage = DfuStage.Uploading,
                                completedPages = completed,
                                totalPages = total,
                                retryCount = retries,
                            )
                        }
                    },
                ).upload(bytes)
            }.onSuccess {
                _uiState.update { it.copy(stage = DfuStage.Completed) }
            }.onFailure { error ->
                val cancelled = error is kotlinx.coroutines.CancellationException
                _uiState.update { it.copy(stage = if (cancelled) DfuStage.Cancelled else DfuStage.Failed) }
                if (!cancelled) {
                    val message = error.message ?: "펌웨어 전송에 실패했습니다."
                    log(message)
                    showMessage(message)
                }
            }
            ticker.cancel()
            _uiState.update { it.copy(elapsedMillis = System.currentTimeMillis() - startedAt) }
        }
    }

    fun cancelUpload() {
        if (!_uiState.value.uploading) return
        uploadJob?.cancel()
    }

    fun clearLogs() = _uiState.update { it.copy(logs = emptyList()) }
    fun consumeMessage() = _uiState.update { it.copy(message = null) }
    fun reportError(message: String) {
        showMessage(message)
        log(message)
    }

    private suspend fun readFirmware(uri: Uri): Pair<String, ByteArray> = withContext(Dispatchers.IO) {
        val resolver = getApplication<Application>().contentResolver
        val name = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) cursor.getString(0) else null
        } ?: uri.lastPathSegment.orEmpty()
        val bytes = resolver.openInputStream(uri)?.use { stream ->
            val output = java.io.ByteArrayOutputStream()
            val buffer = ByteArray(8 * 1024)
            var total = 0
            while (true) {
                val count = stream.read(buffer)
                if (count < 0) break
                total += count
                if (total > DfuProtocol.MAX_FIRMWARE) error("펌웨어는 120 KiB를 초과할 수 없습니다.")
                output.write(buffer, 0, count)
            }
            output.toByteArray()
        } ?: error("선택한 파일을 열 수 없습니다.")
        name to bytes
    }

    private fun log(message: String) {
        val timestamp = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault()).format(Date())
        _uiState.update { state -> state.copy(logs = (state.logs + "[$timestamp] $message").takeLast(500)) }
    }

    private fun showMessage(message: String) = _uiState.update { it.copy(message = message) }

    override fun onCleared() {
        ble.close()
        super.onCleared()
    }
}
