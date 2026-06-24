package com.example.bootloader.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.bootloader.ble.BleConnectionState
import com.example.bootloader.ble.BleDeviceItem
import java.util.Locale

@Composable
fun DfuScreen(
    state: DfuUiState,
    onScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnect: (BleDeviceItem) -> Unit,
    onReconnect: () -> Unit,
    onDisconnect: () -> Unit,
    onChooseFile: () -> Unit,
    onStart: () -> Unit,
    onCancel: () -> Unit,
    onClearLogs: () -> Unit,
    onMessageShown: () -> Unit,
) {
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(state.message) {
        state.message?.let { snackbar.showSnackbar(it); onMessageShown() }
    }
    Scaffold(snackbarHost = { SnackbarHost(snackbar) }) { padding ->
        Column(
            modifier = Modifier.fillMaxSize().padding(padding).padding(horizontal = 16.dp),
        ) {
            Text(
                "BLE Firmware Updater",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.padding(top = 12.dp, bottom = 10.dp),
            )
            Column(
                modifier = Modifier.weight(1f).verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                ConnectionCard(state, onScan, onStopScan, onConnect, onReconnect, onDisconnect)
                FirmwareCard(state, onChooseFile)
                UploadCard(state, onStart, onCancel)
                LogCard(state.logs, onClearLogs)
                Spacer(Modifier.height(8.dp))
            }
        }
    }
}

@Composable
private fun ConnectionCard(
    state: DfuUiState,
    onScan: () -> Unit,
    onStopScan: () -> Unit,
    onConnect: (BleDeviceItem) -> Unit,
    onReconnect: () -> Unit,
    onDisconnect: () -> Unit,
) = SectionCard("BLE 연결") {
    val connected = state.bleState == BleConnectionState.Connected
    Text("상태: ${bleStateText(state.bleState)}")
    Text("장치: ${state.selectedDevice?.let { "${it.name} (${it.address})" } ?: "선택 안 됨"}")
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        if (state.bleState == BleConnectionState.Scanning) {
            OutlinedButton(onClick = onStopScan, enabled = !state.uploading) { Text("검색 중지") }
        } else {
            Button(onClick = onScan, enabled = !state.uploading) { Text("BLE 검색") }
        }
        OutlinedButton(
            onClick = if (connected) onDisconnect else onReconnect,
            enabled = !state.uploading && (connected || state.selectedDevice != null),
        ) { Text(if (connected) "연결 해제" else "재연결") }
    }
    if (state.devices.isNotEmpty() && !connected) {
        Text("검색 결과", fontWeight = FontWeight.SemiBold)
        state.devices.forEach { device ->
            HorizontalDivider()
            Row(
                modifier = Modifier.fillMaxWidth().padding(vertical = 5.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text(device.name, fontWeight = FontWeight.Medium)
                    Text("${device.address}  RSSI ${device.rssi} dBm", style = MaterialTheme.typography.bodySmall)
                }
                TextButton(onClick = { onConnect(device) }) { Text("연결") }
            }
        }
    }
}

@Composable
private fun FirmwareCard(state: DfuUiState, onChooseFile: () -> Unit) = SectionCard("펌웨어") {
    val firmware = state.firmware
    if (state.loadingFile) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CircularProgressIndicator(Modifier.width(20.dp), strokeWidth = 2.dp)
            Spacer(Modifier.width(8.dp)); Text("파일 읽는 중...")
        }
    } else if (firmware == null) {
        Text("선택된 .bin 파일이 없습니다.")
    } else {
        Text(firmware.name, fontWeight = FontWeight.Medium)
        Text("${formatBytes(firmware.size)} · ${firmware.pages} 페이지")
    }
    OutlinedButton(onClick = onChooseFile, enabled = !state.uploading && !state.loadingFile) {
        Text(".bin 파일 선택")
    }
}

@Composable
private fun UploadCard(state: DfuUiState, onStart: () -> Unit, onCancel: () -> Unit) =
    SectionCard("DFU 전송") {
        val fraction = if (state.totalPages == 0) 0f
            else state.completedPages.toFloat() / state.totalPages
        LinearProgressIndicator(progress = { fraction }, modifier = Modifier.fillMaxWidth())
        Text("${(fraction * 100).toInt()}%  ·  ${state.completedPages}/${state.totalPages} 페이지")
        Text("누적 재시도 ${state.retryCount}회  ·  경과 ${formatElapsed(state.elapsedMillis)}")
        Text("결과: ${stageText(state.stage)}")
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = onStart, enabled = state.canStart) { Text("전송 시작") }
            OutlinedButton(onClick = onCancel, enabled = state.uploading) { Text("중단") }
        }
    }

@Composable
private fun LogCard(logs: List<String>, onClearLogs: () -> Unit) = SectionCard("로그") {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End) {
        TextButton(onClick = onClearLogs) { Text("로그 지우기") }
    }
    Box(
        Modifier.fillMaxWidth().height(220.dp).background(MaterialTheme.colorScheme.surfaceVariant).padding(8.dp),
    ) {
        if (logs.isEmpty()) Text("로그가 없습니다.", style = MaterialTheme.typography.bodySmall)
        else LazyColumn(Modifier.fillMaxSize()) {
            items(logs) { line -> Text(line, fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall) }
        }
    }
}

@Composable
private fun SectionCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.Bold)
            content()
        }
    }
}

private fun bleStateText(value: BleConnectionState) = when (value) {
    BleConnectionState.Idle -> "대기"
    BleConnectionState.Scanning -> "검색 중"
    BleConnectionState.Connecting -> "연결 중"
    BleConnectionState.Discovering -> "서비스 확인 중"
    BleConnectionState.Connected -> "연결됨"
    BleConnectionState.Disconnected -> "연결 해제됨"
    BleConnectionState.Failed -> "오류"
}

private fun stageText(value: DfuStage) = when (value) {
    DfuStage.Idle -> "대기"
    DfuStage.Handshaking -> "핸드셰이크 중"
    DfuStage.Uploading -> "전송 중"
    DfuStage.Completed -> "펌웨어 전송 완료"
    DfuStage.Failed -> "전송 실패"
    DfuStage.Cancelled -> "사용자 중단"
}

private fun formatBytes(bytes: Int): String = String.format(Locale.getDefault(), "%.1f KiB", bytes / 1024.0)
private fun formatElapsed(ms: Long): String = String.format(Locale.getDefault(), "%.1f초", ms / 1000.0)
