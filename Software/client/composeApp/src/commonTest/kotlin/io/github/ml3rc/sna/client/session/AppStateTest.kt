package io.github.ml3rc.sna.client.session

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

class AppStateTest {
    @Test
    fun disconnectCancelsPendingConnectionAndAllowsRetry() = runBlocking {
        val transport = RetryTransport().also { it.failOpen = false; it.respond = false }
        val model = AppState(transport)
        val port = SerialPortInfo("test", "Simulated device")
        try {
            model.connect(port) {}
            withTimeout(3000) { while (!transport.isOpen) delay(10) }
            model.disconnect()
            transport.respond = true
            model.connect(port) {}
            withTimeout(3000) { while (!model.connected.value || model.connecting.value) delay(10) }
            assertNotNull(model.session.value)
            assertEquals("test", model.session.value?.info?.fw)
            val sweep = model.session.value!!.sweep
            sweep.update { it.copy(f0 = "50000000", f1 = "60000000", points = "-1") }
            sweep.start()
            assertFalse(sweep.running.value)
            assertEquals("Sweep points must be in 1..4096", sweep.error.value)
        } finally { model.close() }
    }

    @Test
    fun cableRemovalResetsConnectedState() = runBlocking {
        val transport = RetryTransport().also { it.failOpen = false }
        val model = AppState(transport)
        try {
            model.connect(SerialPortInfo("test", "Simulated device")) {}
            withTimeout(3000) { while (!model.connected.value || model.connecting.value) delay(10) }
            transport.endInput()
            withTimeout(3000) { while (model.connected.value) delay(10) }
            assertNull(model.session.value)
                        assertEquals("Device disconnected", model.error.value)
        } finally { model.close() }
    }
    @Test
    fun successfulReconnectClearsPreviousPortError() = runBlocking {
        val transport = RetryTransport()
        val model = AppState(transport)
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
            assertEquals("test", model.session.value?.info?.fw)
        } finally {
            model.disconnect()
        }
    }

    @Test
    fun firmwareSpeakingAnotherProtocolIsRefused() = runBlocking {
        for ((proto, hint) in listOf(2 to "Update the firmware.", 4 to "Update the client.")) {
            val transport = RetryTransport().also { it.failOpen = false; it.proto = proto }
            val model = AppState(transport)
            try {
                model.connect(SerialPortInfo("test", "Simulated device")) {}
                withTimeout(3000) { while (model.connecting.value || model.error.value == null) delay(10) }
                assertFalse(model.connected.value)
                assertNull(model.session.value)
                assertEquals(true, model.error.value?.contains("protocol $proto"), model.error.value)
                assertEquals(true, model.error.value?.endsWith(hint), model.error.value)
                assertFalse(transport.isOpen) // the port is released again
            } finally { model.close() }
        }
    }

    @Test
    fun deviceWithAnotherFrequencyRangeIsRefused() = runBlocking {
        val transport = RetryTransport().also { it.failOpen = false; it.freqMaxHz = 4_400_000_000L }
        val model = AppState(transport)
        try {
            model.connect(SerialPortInfo("test", "Simulated device")) {}
            withTimeout(3000) { while (model.connecting.value || model.error.value == null) delay(10) }
            assertFalse(model.connected.value)
            assertNull(model.session.value)
            assertEquals(true, model.error.value?.contains("50000000–4400000000 Hz"), model.error.value)
            assertFalse(transport.isOpen) // the port is released again
        } finally { model.close() }
    }

    private class RetryTransport : SerialTransport {
        var failOpen = true
        var respond = true
        var proto = 3
        var freqMaxHz = 6_000_000_000L
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
            lines.send("""{"id":$id,"ok":true,"fw":"test","proto":$proto,"hw_rev":1,"freq_min_hz":50000000,"freq_max_hz":$freqMaxHz}""")
        }
    }
}
