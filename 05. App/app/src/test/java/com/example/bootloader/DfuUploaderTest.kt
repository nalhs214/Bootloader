package com.example.bootloader

import com.example.bootloader.ble.DfuBleChannel
import com.example.bootloader.dfu.DfuProtocol
import com.example.bootloader.dfu.DfuUploader
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DfuUploaderTest {
    @Test
    fun handshakeAndUpload_succeeds() = runBlocking {
        val fake = FakeChannel(DfuProtocol.HANDSHAKE_READY, DfuProtocol.ACK, DfuProtocol.ACK)
        var completed = 0
        DfuUploader(fake, onProgress = { pages, _, _ -> completed = pages })
            .upload(byteArrayOf(1, 2, 3))
        assertEquals(1, completed)
        assertTrue(fake.writes.first().contentEquals(byteArrayOf(DfuProtocol.HANDSHAKE_START.toByte())))
    }

    @Test
    fun nak_retriesAndSucceeds() = runBlocking {
        val fake = FakeChannel(
            DfuProtocol.HANDSHAKE_READY,
            DfuProtocol.NAK,
            DfuProtocol.ACK,
            DfuProtocol.ACK,
        )
        var retries = 0
        DfuUploader(fake, onProgress = { _, _, value -> retries = value }).upload(byteArrayOf(7))
        assertEquals(1, retries)
    }

    @Test(expected = IllegalStateException::class)
    fun ackTimeoutThreeTimes_fails() = runBlocking {
        val fake = FakeChannel(DfuProtocol.HANDSHAKE_READY, null, null, null)
        DfuUploader(fake).upload(byteArrayOf(7))
    }

    private class FakeChannel(vararg responses: Int?) : DfuBleChannel {
        private val responses = responses.map { it?.toByte() }.toMutableList()
        val writes = mutableListOf<ByteArray>()
        override val connected = true
        override suspend fun write(value: ByteArray) { writes += value }
        override suspend fun receive(timeoutMs: Long): Byte? = if (responses.isEmpty()) null else responses.removeAt(0)
        override fun drainResponses() = Unit
    }
}
