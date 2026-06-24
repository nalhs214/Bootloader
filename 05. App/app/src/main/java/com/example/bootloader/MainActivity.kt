package com.example.bootloader

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import com.example.bootloader.ui.DfuScreen
import com.example.bootloader.ui.DfuViewModel
import com.example.bootloader.ui.theme.BootloaderTheme

class MainActivity : ComponentActivity() {
    private val dfuViewModel: DfuViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            BootloaderTheme(dynamicColor = false) {
                val viewModel = dfuViewModel
                val state by viewModel.uiState.collectAsState()
                val context = LocalContext.current

                val bluetoothLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.StartActivityForResult(),
                ) {
                    if (viewModel.bluetoothEnabled) viewModel.startScan()
                    else viewModel.reportError("Bluetooth가 켜지지 않아 검색할 수 없습니다.")
                }
                val permissionLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.RequestMultiplePermissions(),
                ) { result ->
                    if (result.values.all { it }) {
                        if (viewModel.bluetoothEnabled) viewModel.startScan()
                        else bluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                    } else viewModel.reportError("BLE 검색 및 연결 권한이 거부되었습니다.")
                }
                val firmwareLauncher = rememberLauncherForActivityResult(
                    ActivityResultContracts.OpenDocument(),
                ) { uri -> uri?.let(viewModel::selectFirmware) }

                fun scanWithChecks() {
                    val permissions = requiredBlePermissions()
                    val missing = permissions.filter {
                        ContextCompat.checkSelfPermission(context, it) != PackageManager.PERMISSION_GRANTED
                    }
                    when {
                        missing.isNotEmpty() -> permissionLauncher.launch(missing.toTypedArray())
                        !viewModel.bluetoothAvailable -> viewModel.startScan()
                        !viewModel.bluetoothEnabled ->
                            bluetoothLauncher.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                        else -> viewModel.startScan()
                    }
                }

                DisposableEffect(state.uploading) {
                    if (state.uploading) window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    else window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
                    onDispose { window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
                }

                DfuScreen(
                    state = state,
                    onScan = ::scanWithChecks,
                    onStopScan = viewModel::stopScan,
                    onConnect = viewModel::connect,
                    onReconnect = viewModel::reconnect,
                    onDisconnect = viewModel::disconnect,
                    onChooseFile = { firmwareLauncher.launch(arrayOf("application/octet-stream", "*/*")) },
                    onStart = viewModel::startUpload,
                    onCancel = viewModel::cancelUpload,
                    onClearLogs = viewModel::clearLogs,
                    onMessageShown = viewModel::consumeMessage,
                )
            }
        }
    }
}

private fun requiredBlePermissions(): List<String> = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
    listOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
} else {
    listOf(Manifest.permission.ACCESS_FINE_LOCATION)
}
