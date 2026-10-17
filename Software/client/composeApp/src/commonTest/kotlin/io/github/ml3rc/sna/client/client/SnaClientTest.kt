package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.protocol.CalCurve
import io.github.ml3rc.sna.client.protocol.CalSet
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
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
import kotlinx.serialization.json.jsonArray
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
                client.sweep(start, stop, 2, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList()
            }
            assertFailsWith<IllegalArgumentException> {
                client.calSet(io.github.ml3rc.sna.client.protocol.CalSet.entries.first(),
                    io.github.ml3rc.sna.client.protocol.CalCurve(start, stop, listOf(0.0)))
            }
        }
        assertTrue(io.github.ml3rc.sna.client.protocol.FrequencyLimits.isValidRange(50_000_000L, 6_000_000_000L))
    }

    @Test
    fun everySweepSendsItsAttenuation() = runBlocking<Unit> {
        val lines = Channel<String>(Channel.UNLIMITED)
        val sent = mutableListOf<Int>()
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = lines.receiveAsFlow()
            override suspend fun writeLine(line: String) {
                val request = Json.parseToJsonElement(line).jsonObject
                val id = request.getValue("id").jsonPrimitive.int
                sent += request.getValue("atten").jsonPrimitive.int
                // z, atten and params are required by protocol 3; raw-sweep-only `port` must not be sent.
                assertEquals(50, request.getValue("z").jsonPrimitive.int)
                assertEquals(listOf("s11"), request.getValue("params").jsonArray.map { it.jsonPrimitive.content })
                assertFalse("port" in request || "source" in request)
                assertEquals("false", request.getValue("allow_unlocked").jsonPrimitive.content)
                lines.send("""{"id":$id,"pt":0,"f":50000000,"s11_db":-12.3,"locked":true}""")
                lines.send("""{"id":$id,"ok":true,"done":true}""")
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            for (level in listOf(0, 40)) {
                val result = withTimeout(3000) {
                    client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY,
                        setOf(SParam.S11), attenuation = level, allowUnlocked = false).toList()
                }
                assertEquals(-12.3, result.single().s11_db)
            }
            assertEquals(listOf(0, 40), sent)
            assertFailsWith<IllegalArgumentException> {
                client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 128).toList()
            }
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
                lines.send("""{"id":$id,"pt":0,"f":50000000,"s11_db":-12.3,"locked":false,"cal":false}""")
                lines.send("""{"id":$id,"ok":true,"done":true,"warning":"source_unlocked","unlocked_points":1}""")
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            val points = withTimeout(3000) {
                client.sweep(50000000, 50000000, 1, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList()
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
                    client.sweep(50000000, 6000000000, 201, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList()
                }
            }
            assertEquals("Source not locked at 5100000000 Hz (retune, point 12)", error.message)
            assertEquals("source_not_locked", error.code)
        } finally {
            client.stop()
            lines.close()
        }
    }

    @Test
    fun sdiPointsAcceptTheWholeBandAboveUint32() = runBlocking<Unit> {
        val lines = Channel<String>(Channel.UNLIMITED)
        val sent = mutableListOf<String>()
        val transport = object : SerialTransport {
            override val isOpen = true
            override suspend fun listPorts() = emptyList<SerialPortInfo>()
            override suspend fun open(port: SerialPortInfo) {}
            override fun close() {}
            override fun incomingLines() = lines.receiveAsFlow()
            override suspend fun writeLine(line: String) {
                sent += line
                val id = Json.parseToJsonElement(line).jsonObject.getValue("id").jsonPrimitive.int
                lines.send("""{"id":$id,"ok":true}""")
            }
        }
        val client = SnaClient(transport, this)
        client.start()
        try {
            val point = { f: Long -> io.github.ml3rc.sna.client.protocol.SdiTestPointConfig(f, -12.0, "s21", "gt") }
            withTimeout(3000) { client.sdiTestSet(listOf(point(5_940_000_000L))) }
            assertTrue(sent.single().contains("\"f\":5940000000"))
            for (bad in listOf(listOf(point(6_000_000_001L)), List(16) { point(1_000_000_000L) })) {
                assertFailsWith<IllegalArgumentException> { client.sdiTestSet(bad) }
            }
            assertEquals(1, sent.size)
        } finally {
            client.stop()
            lines.close()
        }
    }

    /** Answers every request with [reply] (given the command and id); records the commands sent. */
    private class ScriptedDevice(val reply: (cmd: String, id: Int) -> String) : SerialTransport {
        val lines = Channel<String>(Channel.UNLIMITED)
        val commands = mutableListOf<String>()
        override val isOpen = true
        override suspend fun listPorts() = emptyList<SerialPortInfo>()
        override suspend fun open(port: SerialPortInfo) {}
        override fun close() {}
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            val request = Json.parseToJsonElement(line).jsonObject
            val cmd = request.getValue("cmd").jsonPrimitive.content
            commands += if (cmd == "mode_enter") "mode_enter ${request.getValue("mode").jsonPrimitive.content}" else cmd
            lines.send(reply(cmd, request.getValue("id").jsonPrimitive.int))
        }
    }

    @Test
    fun failedModeExitKeepsTheModeAndTheNextAcquireLeavesItFirst() = runBlocking {
        var exitWorks = false
        val device = ScriptedDevice { cmd, id ->
            when {
                cmd == "mode_exit" && !exitWorks -> """{"id":$id,"ok":false,"error":"storage_error"}"""
                else -> """{"id":$id,"ok":true}"""
            }
        }
        val client = SnaClient(device, this).also { it.start() }
        try {
            val first = Any()
            client.acquireMode(DeviceMode.GENERATOR, first)
            client.releaseMode(first)
            // The device did not confirm idle, so the client must not assume it can enter another mode...
            val refused = assertFailsWith<SnaProtocolException> { client.acquireMode(DeviceMode.POWER_METER, Any()) }
            assertEquals("The device is in generator mode. Stop that first.", refused.message)
            // ...and once leaving works, the next acquire leaves the orphaned mode before entering.
            exitWorks = true
            val second = Any()
            client.acquireMode(DeviceMode.POWER_METER, second)
            client.releaseMode(second)
            assertEquals(listOf("mode_enter generator", "mode_exit", "mode_exit", "mode_exit", "mode_enter power_meter", "mode_exit"),
                device.commands)
        } finally {
            client.stop()
            device.lines.close()
        }
    }

    @Test
    fun malformedTemperatureReplyIsAProtocolError() = runBlocking {
        var body = ""","temp_c":"hot""""
        val device = ScriptedDevice { _, id -> """{"id":$id,"ok":true$body}""" }
        val client = SnaClient(device, this).also { it.start() }
        try {
            for (bad in listOf(""","temp_c":"hot"""", "", ""","temp_c":null""")) {
                body = bad
                assertFailsWith<SnaProtocolException>(bad) { client.srcTempRead() }
            }
            body = ""","cached":true,"age_ms":120,"temp_c":42.5"""
            val reading = client.srcTempRead()
            assertEquals(42.5, reading?.tempC)
            assertEquals(120L, reading?.ageMs)
        } finally {
            client.stop()
            device.lines.close()
        }
    }

    @Test
    fun calibrationValuesOutsideTheStoredInt16RangeAreRejected() = runBlocking {
        val device = ScriptedDevice { _, id -> """{"id":$id,"ok":true}""" }
        val client = SnaClient(device, this).also { it.start() }
        try {
            for (bad in listOf(-327.69, 327.68)) {
                assertFailsWith<IllegalArgumentException> {
                    client.calSet(CalSet.S11_50, CalCurve(50_000_000L, 6_000_000_000L, listOf(0.0, bad)))
                }
            }
            client.calSet(CalSet.S11_50, CalCurve(50_000_000L, 6_000_000_000L, listOf(ProtocolLimits.DB_MIN, ProtocolLimits.DB_MAX)))
            assertEquals(listOf("cal_set"), device.commands)
        } finally {
            client.stop()
            device.lines.close()
        }
    }
}
