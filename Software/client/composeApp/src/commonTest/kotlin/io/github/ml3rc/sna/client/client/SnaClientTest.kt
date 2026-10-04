package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import io.github.ml3rc.sna.client.data.Touchstone

class SnaClientTest {
    @Test
    fun frequenciesOutsideSupportedBandAreRejectedBeforeTransport() = runBlocking {
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = kotlinx.coroutines.flow.emptyFlow<String>()
            override suspend fun writeLine(line: String) { error("Invalid input reached transport") }
        }
        val client = SnaClient(transport, this)
        for ((start, stop) in listOf(49_999_999L to 6_000_000_000L,
            50_000_000L to 6_000_000_001L, 6_000_000_000L to 50_000_000L)) {
            assertFailsWith<IllegalArgumentException> {
                client.sweep(start, stop, 2, false, Impedance.FIFTY, setOf(SParam.S11)).toList()
            }
            assertFailsWith<IllegalArgumentException> {
                client.calSet(io.github.ml3rc.sna.client.protocol.CalSet.entries.first(),
                    io.github.ml3rc.sna.client.protocol.CalCurve(start, stop, listOf(0.0)))
            }
        }
        assertTrue(io.github.ml3rc.sna.client.protocol.FrequencyLimits.isValidRange(50_000_000L, 6_000_000_000L))
    }

    @Test
    fun maximumLevelRequestRequiresSupportAndUsesZeroAttenuation() = runBlocking {
        val lines = Channel<String>(Channel.UNLIMITED)
        var supported = false
        var sweeps = 0
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = lines.receiveAsFlow()
            override suspend fun writeLine(line: String) {
                val request = Json.parseToJsonElement(line).jsonObject
                val id = request.getValue("id").jsonPrimitive.int
                if (request.getValue("cmd").jsonPrimitive.content == "ping") {
                    lines.send("""{"id":$id,"ok":true,"fw":"0.1.6","hw_rev":1,"proto":1,"sweep_atten":$supported}""")
                } else {
                    sweeps++
                    assertEquals(0, request.getValue("atten").jsonPrimitive.int)
                    assertEquals("false", request.getValue("allow_unlocked").jsonPrimitive.content)
                    lines.send("""{"id":$id,"pt":0,"f":50000000,"s11_db":-12.3,"locked":true}""")
                    lines.send("""{"id":$id,"ok":true,"done":true}""")
                }
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            assertFailsWith<IllegalStateException> {
                withTimeout(3000) {
                    client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY,
                        setOf(SParam.S11), allowUnlocked = false, attenuation = 0).toList()
                }
            }
            assertEquals(0, sweeps)
            supported = true
            val result = withTimeout(3000) {
                client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY,
                    setOf(SParam.S11), allowUnlocked = false, attenuation = 0).toList()
            }
            assertEquals(1, sweeps)
            assertEquals(-12.3, result.single().s11_db)
        } finally {
            client.stop()
            lines.close()
        }
    }

    @Test
    fun unlockedPointIsReturnedAndExportKeepsWarning() = runBlocking {
        val lines = Channel<String>(Channel.UNLIMITED)
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = lines.receiveAsFlow()
            override suspend fun writeLine(line: String) {
                val request = Json.parseToJsonElement(line).jsonObject
                val id = request.getValue("id").jsonPrimitive.int
                assertEquals("true", request.getValue("allow_unlocked").jsonPrimitive.content)
                lines.send("""{"id":$id,"pt":0,"f":50000000,"s11_db":-12.3,"locked":false,"measured":true,"cal":false}""")
                lines.send("""{"id":$id,"ok":true,"done":true,"warning":"source_unlocked","unlocked_points":1}""")
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            val points = withTimeout(3000) {
                client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY, setOf(SParam.S11)).toList()
            }
            assertEquals(-12.3, points.single().s11_db)
            assertFalse(points.single().locked)
            val exported = Touchstone.formatS1P(points, 50)
            assertTrue(exported.contains("Source lock unconfirmed at 50000000 Hz"))
            assertTrue(exported.contains("50000000 -12.3 0.0"))
        } finally {
            client.stop()
            lines.close()
        }
    }

    @Test
    fun lockFailureIncludesFrequencyAndStage() = runBlocking {
        val lines = Channel<String>(Channel.UNLIMITED)
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = lines.receiveAsFlow()
            override suspend fun writeLine(line: String) {
                val id = Json.parseToJsonElement(line).jsonObject.getValue("id").jsonPrimitive.int
                lines.send("""{"id":$id,"ok":false,"done":true,"error":"source_not_locked","stage":"retune","pt":12,"f":5100000000}""")
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            val error = assertFailsWith<SnaProtocolException> {
                withTimeout(3000) {
                    client.sweep(50000000, 6000000000, 201, false, Impedance.FIFTY, setOf(SParam.S11)).toList()
                }
            }
            assertEquals("Source not locked at 5100000000 Hz (retune, point 12)", error.message)
        } finally {
            client.stop()
            lines.close()
        }
    }
}
