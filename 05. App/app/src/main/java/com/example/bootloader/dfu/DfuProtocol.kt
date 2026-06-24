package com.example.bootloader.dfu

object DfuProtocol {
    const val START_CODE = 0xEF
    const val MSG_DATA = 0x01
    const val MSG_END = 0x02
    const val ACK = 0x06
    const val NAK = 0x15
    const val HANDSHAKE_START = 0x53
    const val HANDSHAKE_READY = 0x52
    const val PAGE_SIZE = 256
    const val PACKET_SIZE = 262
    const val CHUNK_SIZE = 20
    const val CHUNK_DELAY_MS = 20L
    const val MAX_RETRY = 3
    const val ACK_TIMEOUT_MS = 5_000L
    const val MAX_FIRMWARE = 120 * 1024

    fun crc16(data: ByteArray, offset: Int = 0, length: Int = data.size): Int {
        var crc = 0xFFFF
        for (index in offset until offset + length) {
            crc = crc xor ((data[index].toInt() and 0xFF) shl 8)
            repeat(8) {
                crc = if ((crc and 0x8000) != 0) {
                    ((crc shl 1) xor 0x1021) and 0xFFFF
                } else {
                    (crc shl 1) and 0xFFFF
                }
            }
        }
        return crc
    }

    fun dataPacket(firmware: ByteArray, pageIndex: Int): ByteArray {
        require(pageIndex >= 0)
        val start = pageIndex * PAGE_SIZE
        require(start < firmware.size)
        return packet(MSG_DATA, firmware.copyOfRange(start, minOf(start + PAGE_SIZE, firmware.size)))
    }

    fun endPacket(): ByteArray = packet(MSG_END, byteArrayOf())

    private fun packet(type: Int, page: ByteArray): ByteArray {
        require(page.size <= PAGE_SIZE)
        val result = ByteArray(PACKET_SIZE) { 0xFF.toByte() }
        result[0] = START_CODE.toByte()
        result[1] = type.toByte()
        result[2] = 0x00
        result[3] = 0x01
        page.copyInto(result, destinationOffset = 4)
        val crc = crc16(result, offset = 1, length = 259)
        result[260] = (crc and 0xFF).toByte()
        result[261] = (crc ushr 8).toByte()
        return result
    }

    fun validateFirmware(bytes: ByteArray): String? = when {
        bytes.isEmpty() -> "빈 펌웨어 파일은 사용할 수 없습니다."
        bytes.size > MAX_FIRMWARE -> "펌웨어는 120 KiB를 초과할 수 없습니다."
        else -> null
    }
}
