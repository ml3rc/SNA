package io.github.ml3rc.sna.client.ui

import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlin.test.assertFalse
import kotlin.test.assertNotNull

class SnaViewModelTest {
    @Test
    fun disconnectCancelsPendingConnectionAndAllowsRetry() = runBlocking {
        val transport = RetryTransport().also { it.failOpen = false; it.respond = false }
        val model = SnaViewModel(transport)
        val port = SerialPortInfo("test", "Simulated device")
        try {
            model.connect(port) {}
            withTimeout(3000) { while (!transport.isOpen) delay(10) }
            model.disconnect()
            transport.respond = true
            model.connect(port) {}
            withTimeout(3000) { while (!model.connected.value || model.connecting.value) delay(10) }
            assertNotNull(model.client)
            assertEquals("test", model.deviceInfo.value?.fw)
            model.startSweep(50_000_000, 60_000_000, -1, false,
                io.github.ml3rc.sna.client.protocol.Impedance.FIFTY,
                setOf(io.github.ml3rc.sna.client.protocol.SParam.S11))
            assertFalse(model.sweeping.value)
            assertEquals("Sweep points must be in 1..4096", model.error.value)
        } finally { model.close() }
    }

    @Test
    fun cableRemovalResetsConnectedState() = runBlocking {
        val transport = RetryTransport().also { it.failOpen = false }
        val model = SnaViewModel(transport)
        try {
            model.connect(SerialPortInfo("test", "Simulated device")) {}
            withTimeout(3000) { while (!model.connected.value || model.connecting.value) delay(10) }
            transport.endInput()
            withTimeout(3000) { while (model.connected.value) delay(10) }
            assertNull(model.client)
            assertFalse(model.sweeping.value)
            assertEquals("Device disconnected", model.error.value)
        } finally { model.close() }
    }
    @Test
    fun successfulReconnectClearsPreviousPortError() = runBlocking {
        val transport = RetryTransport()
        val model = SnaViewModel(transport)
        val port = SerialPortInfo("test", "Simulated device")
        try {
            model.connect(port) {}
            withTimeout(3000) {
                while (model.connecting.value || model.error.value == null) delay(10)
            }
            assertEquals("Failed to open test", model.error.value)
            transport.failOpen = false
            model.connect(port) {}
            withTimeout(3000) {
                while (!model.connected.value || model.connecting.value) delay(10)
            }
            assertNull(model.error.value)
            assertEquals("test", model.deviceInfo.value?.fw)
        } finally {
            model.disconnect()
        }
    }

    private class RetryTransport : SerialTransport {
        var failOpen = true
        var respond = true
        override var isOpen = false
            private set
        private val lines = Channel<String>(Channel.UNLIMITED)
        fun endInput() { lines.close() }
        override suspend fun listPorts() = emptyList<SerialPortInfo>()
        override suspend fun open(port: SerialPortInfo) {
            if (failOpen) error("Failed to open test")
            isOpen = true
        }
        override fun close() { isOpen = false }
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            if (!respond) return
            val id = Json.parseToJsonElement(line).jsonObject.getValue("id").jsonPrimitive.int
            lines.send("""{"id":$id,"ok":true,"fw":"test","hw_rev":1,"proto":1}""")
        }
    }
}
