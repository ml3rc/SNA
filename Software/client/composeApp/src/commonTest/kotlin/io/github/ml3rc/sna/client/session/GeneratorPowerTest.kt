package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProbeDerived
import io.github.ml3rc.sna.client.protocol.ProbeReading
import io.github.ml3rc.sna.client.protocol.ProbeResult
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.launch
import kotlinx.coroutines.delay
import io.github.ml3rc.sna.client.protocol.DeviceInfo
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Mimics the firmware's rules: the generator excludes sweeps and the power meter, and a sweep lets housekeeping answer from cache. */
private class FakeDevice : SerialTransport {
    val lines = Channel<String>(Channel.UNLIMITED)
    val requests = mutableListOf<JsonObject>()
    var genOn = false
    var genF = 0L
    var port = 1
    var z = 50
    var atten = 127
    var lockFails = false
    var mode = "idle" // device mode, entered/left by the client's tools (or "sweep" for a on-board test)
    var sweeping = false
    var initErrors = "" // JSON array contents of status.init_errors
    var rejectSetField: String? = null // `set` answers bad_args naming this field
    private var powerStream: Job? = null // the continuous power meter's event stream
    override val isOpen = true
    override suspend fun listPorts() = emptyList<SerialPortInfo>()
    override suspend fun open(port: SerialPortInfo) {}
    override fun close() { powerStream?.cancel() }
    override fun incomingLines() = lines.receiveAsFlow()
    override suspend fun writeLine(line: String) {
        val request = Json.parseToJsonElement(line).jsonObject
        requests += request
        val id = request.getValue("id").jsonPrimitive.int
        val reply = when (request.getValue("cmd").jsonPrimitive.content) {
            "gen_start" -> if (lockFails) """{"id":$id,"ok":false,"error":"source_not_locked"}""" else {
                genOn = true
                genF = request.getValue("f").jsonPrimitive.long
                port = request.getValue("port").jsonPrimitive.int
                z = request.getValue("z").jsonPrimitive.int
                atten = request.getValue("atten").jsonPrimitive.int
                """{"id":$id,"ok":true}"""
            }
            "gen_stop" -> { genOn = false; """{"id":$id,"ok":true}""" }
            "set" -> {
                val newZ = request["z"]?.jsonPrimitive?.int
                val newAtten = request["atten"]?.jsonPrimitive?.int
                when {
                    rejectSetField != null -> """{"id":$id,"ok":false,"error":"bad_args","field":"$rejectSetField"}"""
                    newZ == null && newAtten == null -> """{"id":$id,"ok":false,"error":"bad_args","field":"z"}"""
                    newZ != null && newZ != 50 && newZ != 75 -> """{"id":$id,"ok":false,"error":"bad_args","field":"z"}"""
                    newAtten != null && newAtten !in 0..127 -> """{"id":$id,"ok":false,"error":"bad_args","field":"atten"}"""
                    else -> {
                        newZ?.let { z = it }; newAtten?.let { atten = it }
                        """{"id":$id,"ok":true,"z":$z,"atten":$atten}"""
                    }
                }
            }
            "mode_enter" -> {
                val wanted = request.getValue("mode").jsonPrimitive.content
                if (mode != "idle") """{"id":$id,"ok":false,"error":"mode_active","mode":"$mode"}"""
                else { mode = wanted; """{"id":$id,"ok":true,"mode":"$mode"}""" }
            }
            "mode_exit" -> { powerStream?.cancel(); mode = "idle"; genOn = false; """{"id":$id,"ok":true,"mode":"idle"}""" }
            // Continuous power meter: the reply, then a few readings as events.
            "power_start" -> if (genOn) """{"id":$id,"ok":false,"error":"wrong_mode","mode":"generator"}""" else {
                val p = request.getValue("port").jsonPrimitive.int
                val pz = request.getValue("z").jsonPrimitive.int
                val interval = request.getValue("interval_ms").jsonPrimitive.int
                lines.send("""{"id":$id,"ok":true,"port":$p,"z":$pz,"interval_ms":$interval}""")
                powerStream?.cancel()
                powerStream = CoroutineScope(Dispatchers.Default).launch {
                    while (true) {
                        if (lines.trySend("""{"event":"power","port":$p,"z":$pz,"raw":1500,"volts":1.209,"dbm":-9.5,"range":"ok"}""").isClosed) break // test over
                        delay(interval.toLong())
                    }
                }
                return
            }
            "power_stop" -> { powerStream?.cancel(); """{"id":$id,"ok":true}""" }
            "status" -> """{"id":$id,"ok":true,"mode":"$mode","sweeping":$sweeping,"z":$z,"port":$port,"atten":$atten,""" +
                """"gen_on":$genOn""" + (if (genOn) ""","gen_f":$genF""" else "") +
                ""","pwr_on":false,"init_errors":[$initErrors]}"""
            "power_read" -> if (genOn) """{"id":$id,"ok":false,"error":"wrong_mode","mode":"generator"}""" else
                """{"id":$id,"ok":true,"port":${request.getValue("port").jsonPrimitive.int},"z":${request.getValue("z").jsonPrimitive.int},"raw":1500,"volts":1.209,"dbm":-9.5,"range":"ok"}"""
            "ping" -> """{"id":$id,"ok":true,"fw":"0.4.0","proto":3,"hw_rev":1,"freq_min_hz":50000000,"freq_max_hz":6000000000}"""
            "sweep" -> {
                val names = (request.getValue("params") as kotlinx.serialization.json.JsonArray).map { it.jsonPrimitive.content }
                val count = request.getValue("points").jsonPrimitive.int
                val start = request.getValue("f0").jsonPrimitive.long
                val stop = request.getValue("f1").jsonPrimitive.long
                repeat(count) { i ->
                    val f = start + (stop - start) * i / (count - 1).coerceAtLeast(1)
                    val fields = names.joinToString("") { n -> ""","${n}_raw":${1500 + i},"${n}_dbm":-9.5""" }
                    lines.send("""{"id":$id,"pt":$i,"f":$f$fields,"locked":true,"cal":false}""")
                }
                """{"id":$id,"ok":true,"done":true}"""
            }
            "probe_read" -> """{"id":$id,"ok":true,"gen_on":$genOn,"port":$port,"z":$z,"atten":$atten,"readings":[""" +
                """{"path":0,"name":"p1_fwd","raw":1500,"min":1495,"max":1505,"volts":1.209,"dbm":-9.5,"range":"ok"},""" +
                """{"path":1,"name":"p1_bwd","raw":1650,"min":1640,"max":1660,"volts":1.33,"dbm":-14.0,"range":"ok"}]}"""
            else -> """{"id":$id,"ok":false,"error":"unknown_cmd"}"""
        }
        lines.send(reply)
    }
}

internal val INFO = DeviceInfo(fw = "test", hw_rev = 1, proto = 3)

private suspend fun awaitTrue(condition: () -> Boolean) {
    withTimeout(3000) { while (!condition()) delay(10) }
}

class GeneratorPowerTest {
    @Test
    fun frequencyFieldAcceptsMegahertzInsideTheBand() {
        assertEquals(1_000_000_000L, parseFrequencyMhz("1000"))
        assertEquals(2_437_500_000L, parseFrequencyMhz(" 2437.5 "))
        assertEquals(433_920_000L, parseFrequencyMhz("433,92"))
        assertEquals(50_000_000L, parseFrequencyMhz("50"))
        assertEquals(6_000_000_000L, parseFrequencyMhz("6000"))
        for (bad in listOf("", "abc", "49.999999", "6000.000001", "-100", "NaN", "Infinity", "1e30")) assertNull(parseFrequencyMhz(bad), bad)
    }

    @Test
    fun interlockRulesLockOutConflictingTools() {
        val idle = ToolActivity()
        for (tool in Tool.entries) assertNull(blockReason(tool, idle), tool.name)
        assertTrue(!idle.busy)

        val generator = ToolActivity(deviceGenOn = true, ownedMode = DeviceMode.GENERATOR, deviceMode = DeviceMode.GENERATOR)
        assertNotNull(blockReason(Tool.SWEEP, generator))
        assertNotNull(blockReason(Tool.CALIBRATION, generator))
        assertNotNull(blockReason(Tool.POWER, generator))
        assertNotNull(blockReason(Tool.CONSOLE, generator)) // no hand-typed commands under a running test
        assertNull(blockReason(Tool.GENERATOR, generator)) // may be re-applied
        assertTrue(generator.isRunning(Tool.GENERATOR) && generator.busy)

        val sweep = ToolActivity(sweeping = true, ownedMode = DeviceMode.SWEEP, deviceMode = DeviceMode.SWEEP, deviceSweeping = true)
        for (tool in listOf(Tool.GENERATOR, Tool.POWER, Tool.CALIBRATION, Tool.SDI, Tool.DIAGNOSTICS, Tool.CONSOLE)) assertNotNull(blockReason(tool, sweep), tool.name)
        assertNull(blockReason(Tool.SWEEP, sweep)) // its own button already reads "Sweeping"
        assertTrue(!sweep.onBoardTestBusy)

        val power = ToolActivity(powerReading = true, ownedMode = DeviceMode.POWER_METER, deviceMode = DeviceMode.POWER_METER)
        assertNotNull(blockReason(Tool.SWEEP, power))
        assertNotNull(blockReason(Tool.GENERATOR, power))
        assertNotNull(blockReason(Tool.CALIBRATION, power))

        val calibration = ToolActivity(calibrating = true, ownedMode = DeviceMode.SWEEP, deviceMode = DeviceMode.SWEEP, deviceSweeping = true)
        assertNotNull(blockReason(Tool.SWEEP, calibration))
        assertNotNull(blockReason(Tool.GENERATOR, calibration))
        assertTrue(calibration.isRunning(Tool.CALIBRATION) && !calibration.isRunning(Tool.SDI))

        // Something this client did not start (on-board button) is shown on the self-test tab and respected.
        val onBoard = ToolActivity(deviceMode = DeviceMode.SWEEP, deviceSweeping = true)
        assertTrue(onBoard.isRunning(Tool.SDI))
        for (tool in Tool.entries) assertNotNull(blockReason(tool, onBoard), tool.name)

        // Manual RF control holds the device until it is left.
        val manual = ToolActivity(ownedMode = DeviceMode.MANUAL, deviceMode = DeviceMode.MANUAL)
        assertTrue(manual.isRunning(Tool.DIAGNOSTICS))
        assertNull(blockReason(Tool.DIAGNOSTICS, manual))
        for (tool in listOf(Tool.SWEEP, Tool.GENERATOR, Tool.POWER, Tool.CONSOLE)) assertNotNull(blockReason(tool, manual), tool.name)
    }

    @Test
    fun aModeEnteredFromTheConsoleIsAdoptedByItsToolAndLocksOutTheOthers() {
        val consoleGenerator = ToolActivity(deviceMode = DeviceMode.GENERATOR, deviceGenOn = true)
        assertEquals(DeviceMode.GENERATOR, consoleGenerator.foreignMode)
        assertTrue(consoleGenerator.isRunning(Tool.CONSOLE) && !consoleGenerator.generatorOn)
        assertNull(blockReason(Tool.CONSOLE, consoleGenerator)) // so mode_exit can be sent
        assertNull(blockReason(Tool.GENERATOR, consoleGenerator)) // the generator tab can take it over
        assertNotNull(blockReason(Tool.SWEEP, consoleGenerator))
        assertNotNull(blockReason(Tool.POWER, consoleGenerator))
        assertEquals("Generator mode (console)", consoleGenerator.label)

        val consoleSweepMode = ToolActivity(deviceMode = DeviceMode.SWEEP)
        assertNull(blockReason(Tool.SWEEP, consoleSweepMode))
        assertNotNull(blockReason(Tool.GENERATOR, consoleSweepMode))
        // Our own mode is never foreign, even before the status poll caught up.
        assertNull(ToolActivity(ownedMode = DeviceMode.SWEEP, deviceMode = DeviceMode.IDLE).foreignMode)
    }

    @Test
    fun generatorStateFollowsTheDeviceNotTheLastCommand() = runBlocking {
        val device = FakeDevice()
        val client = SnaClient(device, this).also { it.start() }
        val tools = Session(client, INFO, this)
        try {
            val output = GeneratorOutput(2, Impedance.SEVENTY_FIVE, 40, 2_000_000_000L)
            tools.generator.start(output)
            awaitTrue { tools.generator.active.value == output }
            val sent = device.requests.single { it.getValue("cmd").jsonPrimitive.content == "gen_start" }
            assertEquals(40, sent.getValue("atten").jsonPrimitive.int)
            assertEquals(2_000_000_000L, sent.getValue("f").jsonPrimitive.long)
            awaitTrue { tools.activity.value.generatorOn }
            assertTrue(tools.blockReason(Tool.SWEEP) != null)

            // The device switches it off behind our back (front button, disconnect, takeover).
            device.genOn = false
            tools.monitor.refresh()
            awaitTrue { tools.generator.active.value == null && !tools.activity.value.generatorOn }
            assertNull(tools.blockReason(Tool.SWEEP))

            // ...or turns it on without us (e.g. after a reconnect).
            device.genOn = true; device.genF = 100_000_000L; device.port = 1; device.z = 50; device.atten = 8
            tools.monitor.refresh()
            awaitTrue { tools.generator.active.value != null }
            assertEquals(GeneratorOutput(1, Impedance.FIFTY, 8, 100_000_000L), tools.generator.active.value)

            // Level and impedance of a running output go through `set`.
            tools.generator.setLevel(20)
            awaitTrue { tools.generator.active.value?.attenSteps == 20 && !tools.generator.busy.value }
            tools.generator.setImpedance(Impedance.SEVENTY_FIVE)
            awaitTrue { tools.generator.active.value?.z == Impedance.SEVENTY_FIVE && !tools.generator.busy.value }
            val sets = device.requests.filter { it.getValue("cmd").jsonPrimitive.content == "set" }
            assertEquals(listOf("atten", "z"), sets.map { (it.keys - setOf("cmd", "id")).single() })
            assertTrue(device.requests.none { it.getValue("cmd").jsonPrimitive.content in setOf("manual", "set_z") })

            tools.generator.stop()
            awaitTrue { tools.generator.active.value == null && !tools.generator.busy.value }
            assertTrue(!device.genOn)
        } finally {
            tools.close(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun failedStartLeavesGeneratorOffAndExplainsWhy() = runBlocking {
        val device = FakeDevice().also { it.lockFails = true }
        val client = SnaClient(device, this).also { it.start() }
        val tools = Session(client, INFO, this)
        try {
            tools.generator.start(GeneratorOutput(1, Impedance.FIFTY, 0, 5_900_000_000L))
            awaitTrue { tools.generator.error.value != null && !tools.generator.busy.value }
            assertTrue(tools.generator.error.value!!.contains("did not lock"))
            assertNull(tools.generator.active.value)
        } finally {
            tools.close(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun powerMeterRunsInTheBackgroundRestartsOnNewSettingsAndYieldsToTheGenerator() = runBlocking {
        val device = FakeDevice()
        val client = SnaClient(device, this).also { it.start() }
        val tools = Session(client, INFO, this)
        val meter = tools.power
        try {
            meter.start(2, Impedance.FIFTY, intervalMs = 10)
            awaitTrue { meter.reading.value?.port == 2 }
            assertEquals(-9.5, meter.reading.value!!.dbm)
            val started = device.requests.single { it.getValue("cmd").jsonPrimitive.content == "power_start" }
            assertEquals(20, started.getValue("interval_ms").jsonPrimitive.int) // clamped to the device's minimum
            assertTrue(device.requests.none { it.getValue("cmd").jsonPrimitive.content == "power_read" }) // streamed, not polled
            awaitTrue { tools.activity.value.powerReading }
            assertNotNull(tools.blockReason(Tool.GENERATOR)) // interlock while reading

            meter.select(1, Impedance.SEVENTY_FIVE) // running -> restarts with the new settings
            awaitTrue { meter.reading.value?.port == 1 && meter.reading.value?.z == 75 }

            // A sweep starting stops the reading instead of colliding with it.
            tools.sweep.update { it.copy(f0 = "100000000", f1 = "200000000", points = "3") }
            tools.sweep.start()
            awaitTrue { !meter.running.value }
            awaitTrue { !tools.sweep.running.value }
            assertNull(tools.sweep.error.value)

            meter.stop()
            assertNull(meter.reading.value)
        } finally {
            tools.close(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun powerMeterReportsDeviceRefusalAndStopsItself() = runBlocking {
        val device = FakeDevice().also { it.genOn = true; it.mode = "generator" } // left on by another client
        val client = SnaClient(device, this).also { it.start() }
        val meter = PowerMeterState(client, this)
        try {
            meter.start(1, Impedance.FIFTY, intervalMs = 10)
            awaitTrue { !meter.running.value && meter.error.value != null }
            assertTrue(meter.error.value!!.contains("generator mode", ignoreCase = true))
        } finally {
            meter.stop(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun statusReportsModeSweepingAndInitErrors() = runBlocking {
        val device = FakeDevice().also { it.mode = "sweep"; it.sweeping = true; it.port = 2; it.initErrors = "\"eeprom\",\"detector\"" }
        val client = SnaClient(device, this).also { it.start() }
        try {
            val status = client.status()
            assertEquals("sweep", status.mode)
            assertTrue(status.sweeping)
            assertEquals(2, status.port)
            assertEquals(listOf("eeprom", "detector"), status.init_errors)
            assertEquals(3, client.ping().proto)
        } finally {
            client.stop(); device.lines.close()
        }
    }

    @Test
    fun setSendsOnlyTheGivenSettingsAndReportsTheRejectedField() = runBlocking {
        val device = FakeDevice()
        val client = SnaClient(device, this).also { it.start() }
        try {
            assertEquals(io.github.ml3rc.sna.client.protocol.DeviceSettings(75, 40), client.set(Impedance.SEVENTY_FIVE, 40))
            assertEquals(io.github.ml3rc.sna.client.protocol.DeviceSettings(75, 12), client.set(atten = 12))
            val last = device.requests.last()
            assertEquals(setOf("cmd", "id", "atten"), last.keys)
            kotlin.test.assertFailsWith<IllegalArgumentException> { client.set() }
            kotlin.test.assertFailsWith<IllegalArgumentException> { client.set(atten = 128) }

            // bad_args names the offending field; the code stays available for matching.
            device.rejectSetField = "atten"
            val error = kotlin.test.assertFailsWith<io.github.ml3rc.sna.client.protocol.SnaProtocolException> { client.set(atten = 5) }
            assertEquals("bad_args", error.code)
            assertEquals("atten", error.field)
            assertEquals("bad_args (atten)", error.message)
        } finally {
            client.stop(); device.lines.close()
        }
    }

    private fun reading(name: String, dbm: Double, range: String = "ok", spreadCounts: Int = 6) =
        ProbeReading(0, name, 1500, 1500 - spreadCounts / 2, 1500 + spreadCounts / 2, 1.2, dbm, range)

    private fun probe(atten: Int, derived: ProbeDerived?, vararg readings: ProbeReading) =
        ProbeResult(gen_on = true, port = 1, z = 50, atten = atten, f = 1_000_000_000L, readings = readings.toList(), derived = derived)

    @Test
    fun probeDiagnosisPointsAtLevelFloorNoiseAndWrongCalibration() {
        val healthy = probe(0, ProbeDerived("s11", -20.0, -22.0, "s21", -3.0, -0.5, true), reading("p1_fwd", -10.0), reading("p1_bwd", -30.0))
        assertEquals(emptyList(), diagnoseProbe(healthy))

        val floor = diagnoseProbe(probe(127, null, reading("p1_fwd", -40.0), reading("p1_bwd", -62.0, range = "low")))
        assertEquals(1, floor.size)
        assertTrue(floor[0].contains("p1_bwd") && floor[0].contains("Lower the attenuation"))

        val clipped = diagnoseProbe(probe(0, null, reading("p1_fwd", 3.0, range = "high")))
        assertTrue(clipped.single().contains("p1_fwd") && clipped.single().contains("clipped"))

        val noisy = diagnoseProbe(probe(0, null, reading("p1_bwd", -20.0, spreadCounts = 100)))
        assertTrue(noisy.single().contains("Unstable") && noisy.single().contains("p1_bwd"))

        // The reported problem: positive calibrated reflection, at a level other than the one calibration used.
        val positive = diagnoseProbe(probe(64, ProbeDerived("s11", 2.0, 3.2, "s21", -3.0, -1.0, true), reading("p1_fwd", -10.0), reading("p1_bwd", -9.0)))
        assertEquals(2, positive.size)
        assertTrue(positive[0].contains("S11".lowercase()) && positive[0].contains("above 0 dB"))
        assertTrue(positive[1].contains("0 dB attenuation") && positive[1].contains("16.00 dB"))
        // Same symptom at the calibration level: no level hint, the cause is elsewhere.
        assertEquals(1, diagnoseProbe(probe(0, ProbeDerived("s11", 2.0, 3.2, "s21", -3.0, -1.0, true), reading("p1_fwd", -10.0))).size)

        val uncal = diagnoseProbe(probe(0, ProbeDerived("s11", 4.0, 4.0, "s21", -3.0, -3.0, false), reading("p1_fwd", -10.0)))
        assertEquals(2, uncal.size)
        assertTrue(uncal[0].contains("No calibration") && uncal[1].contains("normal before calibration"))
    }

    @Test
    fun probeInterlockAndLiveReadout() = runBlocking {
        val probing = ToolActivity(probing = true, ownedMode = DeviceMode.GENERATOR, deviceMode = DeviceMode.GENERATOR)
        assertNotNull(blockReason(Tool.SWEEP, probing))
        assertNotNull(blockReason(Tool.CALIBRATION, probing))
        assertNotNull(blockReason(Tool.POWER, probing))
        assertNull(blockReason(Tool.GENERATOR, probing)) // the probe is used together with the generator
        assertTrue(probing.isRunning(Tool.GENERATOR))
        assertNotNull(blockReason(Tool.GENERATOR, ToolActivity(powerReading = true, ownedMode = DeviceMode.POWER_METER))) // ...but not with the power meter

        val device = FakeDevice()
        val client = SnaClient(device, this).also { it.start() }
        val tools = Session(client, INFO, this)
        try {
            tools.probe.start(intervalMs = 10)
            awaitTrue { tools.probe.result.value?.readings?.size == 2 }
            assertEquals(-9.5, tools.probe.result.value!!.readings[0].dbm)
            awaitTrue { tools.activity.value.probing }
            assertNotNull(tools.blockReason(Tool.POWER))
            val sent = device.requests.first { it.getValue("cmd").jsonPrimitive.content == "probe_read" }
            assertEquals(4, (sent.getValue("paths") as kotlinx.serialization.json.JsonArray).size)
            tools.probe.stop()
            awaitTrue { !tools.probe.running.value }
        } finally {
            tools.close(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun rawDetectorSweepSendsPathsAndPortAndCollectsPoints() = runBlocking {
        val device = FakeDevice()
        val client = SnaClient(device, this).also { it.start() }
        val tools = Session(client, INFO, this)
        try {
            val raw = tools.rawSweep
            raw.update { it.copy(f0 = "100000000", f1 = "200000000", points = "3", port = 2, attenSteps = 10, log = true,
                paths = setOf(io.github.ml3rc.sna.client.protocol.DetectorPath.P2_FWD, io.github.ml3rc.sna.client.protocol.DetectorPath.P1_BWD)) }
            raw.start()
            awaitTrue { !raw.running.value && raw.points.value.all { it.p2_fwd_raw != null } }
            val sent = device.requests.last { it.getValue("cmd").jsonPrimitive.content == "sweep" }
            assertEquals(2, sent.getValue("port").jsonPrimitive.int)
            assertNull(sent["source"])
            assertEquals(10, sent.getValue("atten").jsonPrimitive.int)
            assertEquals(50, sent.getValue("z").jsonPrimitive.int)
            assertEquals(true, sent.getValue("log").jsonPrimitive.content.toBoolean())
            assertEquals(setOf("p2_fwd", "p1_bwd"),
                (sent.getValue("params") as kotlinx.serialization.json.JsonArray).map { it.jsonPrimitive.content }.toSet())
            assertEquals(3, raw.points.value.size)
            assertEquals(1502, raw.points.value[2].p1_bwd_raw)
            assertNull(raw.points.value[0].p1_fwd_raw) // never requested
            assertNull(raw.error.value)
            val csv = raw.toCsv().lines()
            assertEquals("f_hz,p1_bwd_raw,p1_bwd_dbm,p2_fwd_raw,p2_fwd_dbm", csv[0])
            assertEquals("100000000,1500,-9.5,1500,-9.5", csv[1])
            assertNull(tools.blockReason(Tool.GENERATOR)) // idle again
        } finally {
            tools.close(); client.stop(); device.lines.close()
        }
    }

    @Test
    fun rawSweepOccupiesTheDeviceLikeASweep() {
        val raw = ToolActivity(rawSweeping = true, ownedMode = DeviceMode.SWEEP, deviceMode = DeviceMode.SWEEP, deviceSweeping = true)
        assertTrue(raw.isRunning(Tool.SWEEP) && !raw.isRunning(Tool.SDI)) // ours, not the on-board test
        for (tool in listOf(Tool.CALIBRATION, Tool.GENERATOR, Tool.POWER, Tool.SDI, Tool.DIAGNOSTICS, Tool.CONSOLE)) assertNotNull(blockReason(tool, raw), tool.name)
        assertEquals("Raw detector sweep", raw.label)
    }
}
