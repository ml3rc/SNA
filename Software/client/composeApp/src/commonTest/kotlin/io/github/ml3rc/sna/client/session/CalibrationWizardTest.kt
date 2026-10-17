package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

/** Answers what the calibration wizard sends; sweeps come back corrected while [calibrated] is set. */
private class CalDevice : SerialTransport {
    val lines = Channel<String>(Channel.UNLIMITED)
    val requests = mutableListOf<JsonObject>()
    var calibrated = false
    /** When set, a sweep waits for it before answering. */
    var sweepGate: kotlinx.coroutines.CompletableDeferred<Unit>? = null
    override val isOpen = true
    override suspend fun listPorts() = emptyList<SerialPortInfo>()
    override suspend fun open(port: SerialPortInfo) {}
    override fun close() {}
    override fun incomingLines() = lines.receiveAsFlow()
    override suspend fun writeLine(line: String) {
        val request = Json.parseToJsonElement(line).jsonObject
        requests += request
        val id = request.getValue("id").jsonPrimitive.int
        if (request.getValue("cmd").jsonPrimitive.content == "sweep") {
            sweepGate?.await()
            val names = request.getValue("params").jsonArray.map { it.jsonPrimitive.content }
            val count = request.getValue("points").jsonPrimitive.int
            val start = request.getValue("f0").jsonPrimitive.long
            val stop = request.getValue("f1").jsonPrimitive.long
            repeat(count) { i ->
                val f = start + (stop - start) * i / (count - 1).coerceAtLeast(1)
                val values = names.joinToString("") { ""","${it}_db":-1.0""" }
                lines.send("""{"id":$id,"pt":$i,"f":$f$values,"locked":true""" + (if (calibrated) "}" else ""","cal":false}"""))
            }
            lines.send("""{"id":$id,"ok":true,"done":true}""")
        } else lines.send("""{"id":$id,"ok":true,"mode":"idle"}""")
    }
}

private suspend fun eventually(condition: () -> Boolean) {
    withTimeout(3000) { while (!condition()) delay(10) }
}

class CalibrationWizardTest {
    private val f0 = 50_000_000L
    private val f1 = 6_000_000_000L

    @Test
    fun capturesThatComeBackCalibratedAreRejectedAndUploadEndsTheCaptureSession() = runBlocking {
        val device = CalDevice()
        val client = SnaClient(device, this).also { it.start() }
        val scope = CoroutineScope(coroutineContext + Job(coroutineContext[Job]))
        val mode = CalModeWizardState(Impedance.FIFTY, client, scope)
        try {
            mode.beginOrRestart()
            eventually { mode.started.value }

            // A correction is stored (e.g. by another client): the capture would be corrected already.
            device.calibrated = true
            mode.measure(CalStep.OPEN_PORT1, f0, f1, 3)
            eventually { mode.measuringStep.value == null && mode.error.value != null }
            assertTrue(mode.error.value!!.contains("already calibrated"), mode.error.value)
            assertTrue(mode.captures.value.isEmpty())

            device.calibrated = false
            for (step in CalStep.entries) {
                mode.measure(step, f0, f1, 3)
                eventually { mode.measuringStep.value == null && step in mode.captures.value }
            }
            assertNull(mode.error.value)

            mode.computeAndUpload(f0, f1)
            eventually { mode.uploaded.value && !mode.uploading.value }
            assertEquals(4, device.requests.count { it.getValue("cmd").jsonPrimitive.content == "cal_set" })
            // Further sweeps of this mode are corrected, so capturing needs a restart now.
            assertFalse(mode.started.value)
            val sweeps = device.requests.count { it.getValue("cmd").jsonPrimitive.content == "sweep" }
            mode.measure(CalStep.OPEN_PORT1, f0, f1, 3)
            delay(100)
            assertEquals(sweeps, device.requests.count { it.getValue("cmd").jsonPrimitive.content == "sweep" })
        } finally {
            scope.cancel()
            client.stop()
            device.lines.close()
        }
    }

    @Test
    fun wizardCaptureStateDrivesTheInterlockAndCanBeAborted() = runBlocking {
        val device = CalDevice()
        val client = SnaClient(device, this).also { it.start() }
        val session = Session(client, INFO, this)
        val wizard = session.calibration
        try {
            wizard.mode50.beginOrRestart()
            eventually { wizard.mode50.started.value }
            val gate = kotlinx.coroutines.CompletableDeferred<Unit>()
            device.sweepGate = gate
            wizard.mode50.measure(CalStep.THRU, f0, f1, 2)
            eventually { session.activity.value.calibrating }
            assertTrue(session.blockReason(Tool.SWEEP) != null)
            wizard.abortCapture()
            // The fake answers the sweep inside writeLine, so the abort is written once the sweep is answered.
            gate.complete(Unit)
            eventually { device.requests.any { it.getValue("cmd").jsonPrimitive.content == "abort" } }
            eventually { CalStep.THRU in wizard.mode50.captures.value }
            eventually { !session.activity.value.calibrating }
            assertNull(session.blockReason(Tool.SWEEP))
        } finally {
            session.close()
            client.stop()
            device.lines.close()
        }
    }
}
