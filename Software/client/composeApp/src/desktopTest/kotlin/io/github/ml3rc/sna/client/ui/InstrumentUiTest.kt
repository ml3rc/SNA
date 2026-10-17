package io.github.ml3rc.sna.client.ui

import androidx.compose.ui.graphics.toAwtImage
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.runtime.collectAsState
import io.github.ml3rc.sna.client.session.AppState
import io.github.ml3rc.sna.client.session.MarkerSearch
import io.github.ml3rc.sna.client.session.findTraceMarker
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.serialization.json.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import javax.imageio.ImageIO

private val AppState.s get() = session.value!!

class InstrumentUiTest {
    @get:Rule val compose = createComposeRule()

    @Test fun startupConnectsAndWorkspaceNavigationRemainsUsable() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.onNodeWithText("Scalar network analyzer").assertIsDisplayed()
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            capture("desktop-startup.png")
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithText("Start sweep").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("Start sweep").assertIsDisplayed()
            compose.onNodeWithText("S12").performClick().assertIsOn()
            compose.onNodeWithText("S12").performClick().assertIsOff()
            compose.onNodeWithText("Start sweep").performClick()
            compose.waitUntil(5000) { app.s.sweep.points.value.size == 201 && !app.s.sweep.running.value }
            compose.onNodeWithText("Export JSON").assertIsEnabled()
            compose.onNodeWithText("Find minimum").performScrollTo().performClick()
            compose.onNodeWithText("Marker S21").performScrollTo().performClick()
            val expectedMinimum = findTraceMarker(app.s.sweep.points.value,
                io.github.ml3rc.sna.client.protocol.SParam.S21, MarkerSearch.MINIMUM)!!
            compose.onNodeWithTag("marker-readout").assertTextContains("${expectedMinimum.frequencyHz} Hz", substring = true)
            compose.onNodeWithText("Separate").performScrollTo().performClick()
            compose.onNodeWithTag("marker-readout").performScrollTo().assertIsDisplayed()
            compose.onNodeWithText("Find maximum").performScrollTo().performClick()
            val expectedMaximum = findTraceMarker(app.s.sweep.points.value,
                io.github.ml3rc.sna.client.protocol.SParam.S21, MarkerSearch.MAXIMUM)!!
            compose.onNodeWithTag("marker-readout").assertTextContains("${expectedMaximum.frequencyHz} Hz", substring = true)
            compose.onNodeWithText("Merged").performClick()
            compose.onNodeWithText("Find minimum").performClick()
            compose.onNodeWithTag("sweep-chart").performScrollTo().assertIsDisplayed()
            capture("desktop-sweep.png")
            compose.onNodeWithText("Marker off").performClick()
            compose.onNodeWithTag("marker-readout").assertDoesNotExist()
            compose.onNodeWithText("Calibration").performClick()
            compose.onNodeWithText("Calibration (SLOT)").assertIsDisplayed()
            capture("desktop-calibration.png")
            compose.onNodeWithText("Self-test").performClick()
            compose.onNodeWithText("Add row").performClick()
            compose.onNodeWithText("f (Hz)").assertIsDisplayed()
            capture("desktop-self-test.png")
            compose.onNodeWithText("Diagnostics").performClick()
            compose.onNodeWithText("Housekeeping").assertIsDisplayed()
            capture("desktop-diagnostics.png")
            compose.onNodeWithText("Disconnect").performClick()
            compose.onNodeWithText("Scalar network analyzer").assertIsDisplayed()
            org.junit.Assert.assertEquals(1, transport.opens)
        } finally { app.close() }
    }

    @Test fun markerControlsAndChartRemainReachableAtPhoneWidth() {
        val app = AppState(PreviewTransport())
        try {
            app.connect(SerialPortInfo("preview", "Simulated SNA")) {}
            compose.setContent {
                MaterialTheme(colorScheme = io.github.ml3rc.sna.client.ui.theme.SnaDarkColorScheme,
                    typography = io.github.ml3rc.sna.client.ui.theme.SnaCompactTypography) {
                    Box(Modifier.width(380.dp)) { app.session.collectAsState().value?.let { SweepScreen(it, compactUi = true) } }
                }
            }
            compose.waitUntil(5000) { app.connected.value }
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { app.s.sweep.points.value.size == 201 && !app.s.sweep.running.value }
            compose.onNodeWithText("Hold reference").performScrollTo().performClick()
            compose.onNodeWithTag("reference-summary").performScrollTo().assertIsDisplayed()
            compose.onNodeWithText("Difference (dB)").performScrollTo().performClick()
            compose.onNodeWithText("Find minimum").performScrollTo().performClick()
            compose.onNodeWithText("Marker S21").performScrollTo().performClick()
            compose.onNodeWithTag("marker-readout").performScrollTo().assertIsDisplayed()
            compose.onNodeWithTag("sweep-chart").performScrollTo().assertIsDisplayed()
            capture("phone-marker.png")
            compose.onNodeWithText("Clear reference").performScrollTo().performClick()
            compose.onNodeWithTag("reference-summary").assertDoesNotExist()
        } finally { app.close() }
    }

    @Test fun generatorAndPowerMeterTabsDriveTheDevice() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { app.connected.value }

            compose.onAllNodesWithText("Generator")[0].performClick()
            compose.onNodeWithTag("generator-status").assertTextContains("OFF", substring = true)
            compose.onNodeWithText("Turn off").assertIsNotEnabled()
            compose.onNodeWithText("Frequency (MHz)").performTextReplacement("10")
            compose.onNodeWithText("Turn on").assertIsNotEnabled() // below 50 MHz
            compose.onNodeWithText("Frequency (MHz)").performTextReplacement("2450")
            compose.onNodeWithText("75 Ω").performClick()
            compose.onNodeWithText("Turn on").performClick()
            compose.waitUntil(5000) { transport.generatorFrequency == 2_450_000_000L }
            compose.waitUntil(5000) { compose.onAllNodesWithText("Apply changes").fetchSemanticsNodes().isNotEmpty() ||
                compose.onAllNodesWithText("Turn on").fetchSemanticsNodes().isEmpty() }
            compose.onNodeWithTag("generator-status").assertTextContains("ON", substring = true)
            compose.onNodeWithTag("generator-status").assertTextContains("2.45 GHz", substring = true)
            compose.onNodeWithText("Turn off").assertIsEnabled()
            capture("desktop-generator.png")

            // Probe: live per-path readings, derived S-parameters and the explanation for a positive calibrated value.
            compose.onNodeWithText("Start live readout").performScrollTo().performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithTag("probe-table").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithTag("probe-table").assertTextContains("p2_bwd", substring = true)
            compose.onNodeWithTag("probe-derived").assertTextContains("2.40 dB", substring = true)
            compose.onNodeWithTag("probe-hints").assertTextContains("above 0 dB", substring = true)
            compose.onNodeWithTag("probe-hints").assertTextContains("0 dB attenuation", substring = true)
            compose.onNodeWithTag("probe-hints").assertTextContains("p2_bwd", substring = true) // below detector range
            capture("desktop-probe.png")
            // Sweeping is locked out while the generator runs and the probe polls; stop the probe again before moving on.
            compose.onAllNodesWithText("Sweep")[0].performClick()
            compose.onNodeWithTag("sweep-blocked").assertExists()
            compose.onAllNodesWithText("Generator")[0].performClick()
            compose.onNodeWithText("Stop").performScrollTo().performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithText("Start live readout").fetchSemanticsNodes().isNotEmpty() &&
                runCatching { compose.onNodeWithText("Start live readout").assertIsEnabled() }.isSuccess }

            // The generator is running: its menu button shows it, and the tools that would collide are locked out.
            compose.onNodeWithTag("tool-running-GENERATOR", useUnmergedTree = true).assertExists()
            compose.onAllNodesWithText("Power meter")[0].performClick()
            compose.onNodeWithText("Start reading").assertIsNotEnabled()
            compose.onNodeWithTag("power-blocked").assertTextContains("generator is on", substring = true, ignoreCase = true)
            compose.onAllNodesWithText("Sweep")[0].performClick()
            compose.onNodeWithText("Start sweep").performScrollTo().assertIsNotEnabled()
            compose.onNodeWithTag("sweep-blocked").assertExists()
            // Diagnostics stays readable throughout.
            compose.onNodeWithText("Diagnostics").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithText("41.5°C", substring = true).fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("generator").assertExists() // mode row
            compose.onNodeWithTag("init-errors").assertTextContains("synthesizer did not lock", substring = true)

            compose.onAllNodesWithText("Generator")[0].performClick()
            compose.onNodeWithText("Turn off").performClick()
            compose.waitUntil(5000) { transport.generatorFrequency == null }
            compose.waitUntil(5000) { compose.onAllNodesWithTag("tool-running-GENERATOR", useUnmergedTree = true).fetchSemanticsNodes().isEmpty() }

            compose.onAllNodesWithText("Power meter")[0].performClick()
            compose.onNodeWithText("Start reading").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithTag("power-reading").fetchSemanticsNodes().isNotEmpty() &&
                runCatching { compose.onNodeWithTag("power-reading").assertTextContains("-9.5 dBm", substring = true) }.isSuccess }
            capture("desktop-power-meter.png")
            // Reading continues in the background and keeps its menu indicator on other tabs.
            compose.onNodeWithTag("tool-running-POWER", useUnmergedTree = true).assertExists()
            compose.onAllNodesWithText("Sweep")[0].performClick()
            compose.onNodeWithTag("tool-running-POWER", useUnmergedTree = true).assertExists()
            compose.onNodeWithTag("sweep-blocked").assertExists()
            compose.onAllNodesWithText("Power meter")[0].performClick()
            compose.onNodeWithText("Stop reading").performClick()
            compose.onNodeWithTag("power-reading").assertTextContains("— dBm", substring = true)
            compose.onNodeWithTag("tool-running-POWER", useUnmergedTree = true).assertDoesNotExist()
        } finally { app.close() }
    }

    @Test fun rawDetectorModeSweepsTheFourCouplerStates() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { app.connected.value }
            compose.onNodeWithText("Raw detector").performClick()
            compose.onNodeWithText("Raw detector sweep").assertIsDisplayed()
            // Four states by default; drop two, keep Port 1 fwd and Port 2 bwd.
            compose.onNodeWithText("Port 1 bwd").performScrollTo().performClick().assertIsOff()
            compose.onNodeWithText("Port 2 fwd").performScrollTo().performClick().assertIsOff()
            compose.onNodeWithText("Port 2").performScrollTo().performClick()
            compose.onNodeWithText("Start raw sweep").performScrollTo().performClick()
            val raw = app.s.rawSweep
            compose.waitUntil(5000) { !raw.running.value && raw.points.value.size == 201 && raw.points.value.last().p1_fwd_raw != null }
            org.junit.Assert.assertNotNull(raw.points.value.last().p2_bwd_dbm)
            org.junit.Assert.assertNull(raw.points.value.last().p1_bwd_raw)
            compose.onNodeWithTag("raw-chart").performScrollTo().assertIsDisplayed()
            compose.onNodeWithText("ADC counts").performScrollTo().performClick()
            compose.onNodeWithText("Export CSV").assertIsEnabled()
            capture("desktop-raw-sweep.png")
            // The S-parameter view is still one click away and untouched.
            compose.onNodeWithText("S-parameters").performClick()
            compose.onNodeWithText("Start sweep").performScrollTo().assertIsDisplayed()
        } finally { app.close() }
    }

    @Test fun referenceSurvivesTabChangesAndComparesNewSweeps() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { app.connected.value }
            compose.onNodeWithText("Hold reference").assertIsNotEnabled()
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { app.s.sweep.points.value.size == 201 && !app.s.sweep.running.value }
            val heldPoints = app.s.sweep.points.value.toList()
            compose.onNodeWithText("Hold reference").performScrollTo().performClick()
            compose.waitUntil(5000) { app.s.sweep.reference.reference.value != null }
            compose.onNodeWithText("Calibration").performClick()
            compose.onNodeWithText("Sweep").performClick()
            compose.onNodeWithTag("reference-summary").performScrollTo().assertTextContains("201 samples", substring = true)
            transport.transmissionOffset = -3.0
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { !app.s.sweep.running.value && app.s.sweep.points.value.first().s21_db == -5.0 }
            org.junit.Assert.assertEquals(heldPoints, app.s.sweep.reference.reference.value!!.points)
            compose.onNodeWithText("Overlay").performScrollTo().performClick()
            compose.onNodeWithTag("sweep-chart").performScrollTo()
            compose.onNodeWithText("S21 ref", substring = true).assertExists()
            capture("desktop-reference-overlay.png")
            compose.onNodeWithText("Separate").performScrollTo().performClick()
            compose.onNodeWithText("S21 ref", substring = true).assertExists()
            compose.onNodeWithText("Merged").performScrollTo().performClick()
            compose.onNodeWithText("Difference (dB)").performScrollTo().performClick()
            compose.onNodeWithText("Find minimum").performScrollTo().performClick()
            compose.onNodeWithText("Marker S21").performScrollTo().performClick()
            compose.onNodeWithTag("marker-readout").assertTextContains("Δ S21", substring = true)
            compose.onNodeWithTag("marker-readout").assertTextContains("-3.00 dB", substring = true)
            compose.onNodeWithTag("sweep-chart").performScrollTo()
            capture("desktop-reference-difference.png")
            compose.onNodeWithText("Stop (Hz)").performScrollTo().performTextReplacement("5000000000")
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { !app.s.sweep.running.value && app.s.sweep.points.value.last().f == 5_000_000_000 }
            compose.onNodeWithTag("reference-mismatch").performScrollTo().assertIsDisplayed()
            compose.onNodeWithText("Clear reference").performScrollTo().performClick()
            compose.onNodeWithTag("reference-summary").assertDoesNotExist()
            compose.onNodeWithText("Magnitude response (dB)").assertExists()
        } finally { app.close() }
    }

    @Test fun runningSweepLocksItsSettingsButKeepsTheLevelLive() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { app.connected.value }
            compose.onNodeWithText("Log").performClick()
            val gate = kotlinx.coroutines.CompletableDeferred<Unit>()
            transport.sweepGate = gate
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { app.s.sweep.running.value }
            compose.onNodeWithText("Start (Hz)").assertIsNotEnabled()
            compose.onNodeWithText("Lin").assertIsNotEnabled()
            compose.onNodeWithText("S12").assertIsNotEnabled()
            compose.onNodeWithText("Abort").assertIsEnabled()
            capture("desktop-sweep-running.png")
            // Other tools are locked out, the console too.
            compose.onAllNodesWithText("Generator")[0].performClick()
            compose.onNodeWithText("Turn on").assertIsNotEnabled()
            compose.onNodeWithText("Frequency (MHz)").assertIsNotEnabled()
            compose.onNodeWithText("Console").performClick()
            compose.onNodeWithTag("console-blocked").assertExists()
            gate.complete(Unit)
            compose.waitUntil(5000) { !app.s.sweep.running.value }
            val sweep = transport.sent.last { it.getValue("cmd").jsonPrimitive.content == "sweep" }
            org.junit.Assert.assertEquals("true", sweep.getValue("log").jsonPrimitive.content)
            // The log grid ends exactly on the stop frequency, like the firmware's.
            org.junit.Assert.assertEquals(6_000_000_000L, app.s.sweep.points.value.last().f)
        } finally { app.close() }
    }

    @Test fun consoleShowsTrafficAndSendsLinesWhileIdle() {
        val transport = PreviewTransport()
        val app = AppState(transport)
        try {
            compose.setContent { App(app) }
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { app.connected.value }
            compose.onNodeWithText("Console").performClick()
            compose.onNodeWithTag("console-blocked").assertDoesNotExist()
            compose.onNodeWithTag("console-input").performTextReplacement("""{"cmd":"src_temp_read"}""")
            compose.onNodeWithText("Send").performClick()
            compose.waitUntil(5000) { transport.sent.any { it["id"]?.jsonPrimitive?.content == "console-1" } }
            compose.waitUntil(5000) { compose.onAllNodesWithText("41.5", substring = true).fetchSemanticsNodes().isNotEmpty() }
            // The newest line sits at the bottom of the log, not somewhere in the middle.
            compose.waitForIdle()
            val logBottom = compose.onNodeWithTag("console-log").fetchSemanticsNode().boundsInRoot.bottom
            val newestBottom = compose.onNodeWithText("41.5", substring = true).fetchSemanticsNode().boundsInRoot.bottom
            org.junit.Assert.assertEquals(logBottom, newestBottom, 2f)
            // Background polling is hidden unless asked for.
            compose.onAllNodesWithText("\"cmd\":\"status\",\"id\"", substring = true).assertCountEquals(0)
            compose.onNodeWithText("Polling").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithText("\"cmd\":\"status\",\"id\"", substring = true).fetchSemanticsNodes().isNotEmpty() }
            capture("desktop-console.png")
        } finally { app.close() }
    }

    private fun capture(name: String) {
        val directory = File("build/ui-previews").apply { mkdirs() }
        ImageIO.write(compose.onRoot().captureToImage().toAwtImage(), "png", File(directory, name))
    }

    private class PreviewTransport : SerialTransport {
        var opens = 0
        var transmissionOffset = 0.0
        var generatorFrequency: Long? = null
        /** When set, a sweep waits for it before answering. */
        var sweepGate: kotlinx.coroutines.CompletableDeferred<Unit>? = null
        val sent = mutableListOf<JsonObject>()
        private var powerStream: Job? = null // the continuous power meter's event stream
        override var isOpen = false
        private val lines = Channel<String>(Channel.UNLIMITED)
        override suspend fun listPorts() = listOf(SerialPortInfo("preview", "Simulated SNA · USB"))
        override suspend fun open(port: SerialPortInfo) { opens++; isOpen = true }
        override fun close() { isOpen = false; powerStream?.cancel() }
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            val request = Json.parseToJsonElement(line).jsonObject
            val id = request.getValue("id")
            sent += request
            if (request.getValue("cmd").jsonPrimitive.content == "sweep") {
                sweepGate?.await()
                val count = request.getValue("points").jsonPrimitive.int
                val start = request.getValue("f0").jsonPrimitive.long
                val stop = request.getValue("f1").jsonPrimitive.long
                if (request["port"] != null) { // raw detector sweep
                    val names = request.getValue("params").jsonArray.map { it.jsonPrimitive.content }
                    repeat(count) { index ->
                        val t = index.toDouble() / (count - 1).coerceAtLeast(1)
                        val frequency = start + ((stop - start) * t).toLong()
                        val fields = names.mapIndexed { k, n ->
                            ""","${n}_raw":${1400 + k * 120 + (t * 80).toInt()},"${n}_dbm":${-12.0 - k * 4 - t * 2}"""
                        }.joinToString("")
                        lines.send("""{"id":$id,"pt":$index,"f":$frequency$fields,"locked":true,"cal":false}""")
                    }
                    lines.send("""{"id":$id,"ok":true,"done":true}""")
                    return
                }
                repeat(count) { index ->
                    val t = index.toDouble() / (count - 1).coerceAtLeast(1)
                    val frequency = start + ((stop - start) * t).toLong()
                    val distance = (t - .52) / .075
                    val dip = kotlin.math.exp(-distance * distance)
                    val transmission = -2.0 - 24.0 * dip - 1.2 * t + transmissionOffset
                    val reflection = -18.0 + 13.0 * dip + 1.3 * kotlin.math.sin(t * 18.0)
                    lines.send("""{"id":$id,"pt":$index,"f":$frequency,"s11_db":$reflection,"s21_db":$transmission,"locked":true,"cal":true}""")
                }
                lines.send("""{"id":$id,"ok":true,"done":true}""")
                return
            }
            val response = when (request.getValue("cmd").jsonPrimitive.content) {
                "cal_get" -> """{"id":$id,"ok":false,"error":"not_calibrated"}"""
                "gen_start" -> { generatorFrequency = request.getValue("f").jsonPrimitive.long; """{"id":$id,"ok":true}""" }
                "gen_stop" -> { generatorFrequency = null; """{"id":$id,"ok":true}""" }
                "probe_read" -> """{"id":$id,"ok":true,"gen_on":${generatorFrequency != null},"port":1,"z":50,"atten":64,"f":${generatorFrequency ?: 0},"readings":[""" +
                    """{"path":0,"name":"p1_fwd","raw":1500,"min":1495,"max":1505,"volts":1.209,"dbm":-9.5,"range":"ok"},""" +
                    """{"path":1,"name":"p1_bwd","raw":1600,"min":1590,"max":1610,"volts":1.289,"dbm":-12.7,"range":"ok"},""" +
                    """{"path":2,"name":"p2_fwd","raw":1200,"min":1195,"max":1205,"volts":0.967,"dbm":-19.5,"range":"ok"},""" +
                    """{"path":3,"name":"p2_bwd","raw":300,"min":296,"max":304,"volts":0.242,"dbm":-60.0,"range":"low"}],""" +
                    """"derived":{"refl":"s11","refl_db":3.2,"refl_cal_db":2.4,"trans":"s21","trans_db":-30.0,"trans_cal_db":-0.8,"cal":true}}"""
                "src_temp_read" -> """{"id":$id,"ok":true,"cached":false,"temp_c":41.5}"""
                "status" -> """{"id":$id,"ok":true,"mode":"${if (generatorFrequency != null) "generator" else "idle"}","sweeping":false,"z":50,"port":1,"atten":64,"gen_on":${generatorFrequency != null}""" +
                    (generatorFrequency?.let { ""","gen_f":$it""" } ?: "") + ""","pwr_on":false,"init_errors":["source_lock"]}"""
                "power_read" -> if (generatorFrequency != null) """{"id":$id,"ok":false,"error":"wrong_mode","mode":"generator"}"""
                    else """{"id":$id,"ok":true,"port":1,"z":50,"raw":1500,"volts":1.209,"dbm":-9.5}"""
                // Continuous power meter: the reply, then readings as events.
                "power_start" -> if (generatorFrequency != null) """{"id":$id,"ok":false,"error":"wrong_mode","mode":"generator"}""" else {
                    val port = request.getValue("port").jsonPrimitive.int
                    val z = request.getValue("z").jsonPrimitive.int
                    lines.send("""{"id":$id,"ok":true,"port":$port,"z":$z,"interval_ms":250}""")
                    powerStream?.cancel()
                    powerStream = CoroutineScope(Dispatchers.Default).launch {
                        while (true) {
                            if (lines.trySend("""{"event":"power","port":$port,"z":$z,"raw":1500,"volts":1.209,"dbm":-9.5,"range":"ok"}""").isClosed) break // test over
                            delay(100)
                        }
                    }
                    return
                }
                "power_stop", "mode_exit" -> { powerStream?.cancel(); """{"id":$id,"ok":true,"mode":"idle"}""" }
                "set" -> """{"id":$id,"ok":true,"z":${request["z"]?.jsonPrimitive?.int ?: 50},"atten":${request["atten"]?.jsonPrimitive?.int ?: 64}}"""
                else -> """{"id":$id,"ok":true,"fw":"preview","proto":3,"hw_rev":1,"freq_min_hz":50000000,"freq_max_hz":6000000000}"""
            }
            lines.send(response)
        }
    }
}
