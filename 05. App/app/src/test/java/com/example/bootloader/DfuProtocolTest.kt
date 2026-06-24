package com.example.bootloader

import com.example.bootloader.dfu.DfuProtocol
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class DfuProtocolTest {
    @Test
    fun crc16_matchesCcittFalseCheckValue() {
        assertEquals(0x29B1, DfuProtocol.crc16("123456789".encodeToByteArray()))
    }

    @Test
    fun dataPacket_hasExpectedHeaderLengthPaddingAndCrc() {
        val firmware = byteArrayOf(1, 2, 3)
        val packet = DfuProtocol.dataPacket(firmware, 0)

        assertEquals(262, packet.size)
        assertEquals(0xEF, packet[0].toInt() and 0xFF)
        assertEquals(DfuProtocol.MSG_DATA, packet[1].toInt() and 0xFF)
        assertArrayEquals(byteArrayOf(0x00, 0x01), packet.copyOfRange(2, 4))
        assertArrayEquals(firmware, packet.copyOfRange(4, 7))
        assertTrue(packet.copyOfRange(7, 260).all { it == 0xFF.toByte() })

        val crc = DfuProtocol.crc16(packet, 1, 259)
        assertEquals(crc and 0xFF, packet[260].toInt() and 0xFF)
        assertEquals(crc ushr 8, packet[261].toInt() and 0xFF)
    }

    @Test
    fun fullPage_isNotPadded() {
        val firmware = ByteArray(256) { it.toByte() }
        val packet = DfuProtocol.dataPacket(firmware, 0)
        assertArrayEquals(firmware, packet.copyOfRange(4, 260))
    }

    @Test
    fun endPacket_hasEndTypeFixedLengthAndFfPayload() {
        val packet = DfuProtocol.endPacket()
        assertEquals(262, packet.size)
        assertEquals(DfuProtocol.MSG_END, packet[1].toInt() and 0xFF)
        assertArrayEquals(byteArrayOf(0x00, 0x01), packet.copyOfRange(2, 4))
        assertTrue(packet.copyOfRange(4, 260).all { it == 0xFF.toByte() })
    }

    @Test
    fun firmwareSizeBoundary_accepts120KiBAndRejectsLarger() {
        assertNull(DfuProtocol.validateFirmware(ByteArray(120 * 1024)))
        assertTrue(DfuProtocol.validateFirmware(ByteArray(120 * 1024 + 1)) != null)
    }
}
