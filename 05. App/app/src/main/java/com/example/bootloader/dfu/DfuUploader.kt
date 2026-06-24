package com.example.bootloader.dfu

import com.example.bootloader.ble.DfuBleChannel
import kotlinx.coroutines.CancellationException
import kotlin.math.ceil

class DfuUploader(
    private val channel: DfuBleChannel,
    private val onLog: (String) -> Unit = {},
    private val onProgress: (completed: Int, total: Int, retries: Int) -> Unit = { _, _, _ -> },
) {
    suspend fun upload(firmware: ByteArray) {
        require(DfuProtocol.validateFirmware(firmware) == null)
        try {
            handshake()
            val pages = ceil(firmware.size / DfuProtocol.PAGE_SIZE.toDouble()).toInt()
            var retries = 0
            repeat(pages) { page ->
                retries += sendWithAck(DfuProtocol.dataPacket(firmware, page), "페이지 ${page + 1}")
                onProgress(page + 1, pages, retries)
            }
            retries += sendWithAck(DfuProtocol.endPacket(), "END")
            onProgress(pages, pages, retries)
            onLog("펌웨어 전송 완료")
        } catch (cancelled: CancellationException) {
            onLog("사용자가 전송을 중단했습니다.")
            throw cancelled
        }
    }

    private suspend fun handshake() {
        channel.drainResponses()
        for (attempt in 1..150) {
            ensureConnected()
            channel.write(byteArrayOf(DfuProtocol.HANDSHAKE_START.toByte()))
            val response = channel.receive(200)
            if (response?.toInt()?.and(0xFF) == DfuProtocol.HANDSHAKE_READY) {
                onLog("부트로더 준비 응답 수신")
                return
            }
            if (response != null) logUnknown(response)
            if (attempt == 1 || attempt % 25 == 0) onLog("핸드셰이크 시도 $attempt/150")
        }
        error("핸드셰이크 시간이 초과되었습니다.")
    }

    private suspend fun sendWithAck(packet: ByteArray, label: String): Int {
        var retries = 0
        repeat(DfuProtocol.MAX_RETRY) { attempt ->
            ensureConnected()
            channel.drainResponses()
            packet.asList().chunked(DfuProtocol.CHUNK_SIZE).forEach { chunk ->
                channel.write(chunk.toByteArray())
            }
            while (true) {
                when (val response = channel.receive(DfuProtocol.ACK_TIMEOUT_MS)) {
                    DfuProtocol.ACK.toByte() -> return retries
                    DfuProtocol.NAK.toByte(), null -> {
                        retries++
                        onLog("$label ${if (response == null) "ACK 시간 초과" else "NAK"}, 시도 ${attempt + 1}/3")
                        break
                    }
                    else -> logUnknown(response)
                }
            }
        }
        error("$label 전송 재시도 횟수를 초과했습니다.")
    }

    private fun ensureConnected() {
        check(channel.connected) { "BLE 연결이 해제되었습니다." }
    }

    private fun logUnknown(value: Byte) {
        onLog("알 수 없는 응답: 0x%02X".format(value.toInt() and 0xFF))
    }
}
