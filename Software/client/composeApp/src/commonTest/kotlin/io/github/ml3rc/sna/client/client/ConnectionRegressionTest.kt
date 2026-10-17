package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.data.Touchstone
import io.github.ml3rc.sna.client.protocol.*
import io.github.ml3rc.sna.client.transport.*
import io.github.ml3rc.sna.client.session.CalModeWizardState
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.*
import kotlin.test.*

class ConnectionRegressionTest {
    private class Link : SerialTransport {
        val lines = Channel<String>(Channel.UNLIMITED)
        var respond: suspend (JsonObject) -> Unit = {}
        override val isOpen = true
        override suspend fun listPorts() = emptyList<SerialPortInfo>()
        override suspend fun open(port: SerialPortInfo) {}
        override fun close() {}
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) = respond(Json.parseToJsonElement(line).jsonObject)
        suspend fun send(request: JsonObject, body: String) {
            lines.send("{\"id\":${request.getValue("id")},$body}")
        }
    }

    @Test fun cancelledSweepDoesNotBlockSubsequentCommands() = runBlocking<Unit> {
        val link = Link()
        val client = SnaClient(link, this)
        link.respond = { request ->
            if (request.getValue("cmd").jsonPrimitive.content == "sweep") {
                repeat(4096) { i -> link.send(request, "\"pt\":$i,\"f\":50000000,\"s11_db\":-10,\"locked\":true") }
                link.send(request, "\"ok\":true,\"done\":true")
            } else link.send(request, "\"ok\":true,\"fw\":\"test\",\"proto\":3,\"hw_rev\":1,\"freq_min_hz\":50000000,\"freq_max_hz\":6000000000")
        }
        client.start()
        try {
            withTimeout(3000) {
                client.sweep(50_000_000, 50_000_000, 4096, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).take(1).toList()
                assertEquals("test", client.ping().fw)
            }
        } finally { client.stop() }
    }

    @Test fun disconnectedStreamFailsInsteadOfReportingSuccess() = runBlocking<Unit> {
        val link = Link()
        val client = SnaClient(link, this)
        link.respond = { request ->
            link.send(request, "\"pt\":0,\"f\":50000000,\"s11_db\":-10,\"locked\":true")
            link.lines.close()
        }
        client.start()
        try {
            assertFailsWith<SnaProtocolException> {
                withTimeout(3000) { client.sweep(50_000_000, 60_000_000, 2, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList() }
            }
        } finally { client.stop() }
    }

    @Test fun rejectedSweepAndCalibrationErrorsKeepTheirDeviceError() = runBlocking<Unit> {
        val link = Link()
        val client = SnaClient(link, this)
        link.respond = { link.send(it, "\"ok\":false,\"error\":\"busy\"") }
        client.start()
        try {
            assertEquals("busy", assertFailsWith<SnaProtocolException> {
                client.sweep(50_000_000, 60_000_000, 2, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList()
            }.message)
            assertEquals("busy", assertFailsWith<SnaProtocolException> { client.calGet(CalSet.entries.first()) }.message)
            val wizard = CalModeWizardState(Impedance.FIFTY, client, this)
            wizard.beginOrRestart()
            withTimeout(3000) { wizard.uploading.first { !it } }
            assertFalse(wizard.started.value)
            assertEquals("busy", wizard.error.value)
        } finally { client.stop() }
    }

    @Test fun incompleteSuccessfulSweepIsRejected() = runBlocking<Unit> {
        val link = Link()
        val client = SnaClient(link, this)
        link.respond = { link.send(it, "\"ok\":true,\"done\":true") }
        client.start()
        try {
            assertFailsWith<IllegalStateException> {
                client.sweep(50_000_000, 60_000_000, 2, false, Impedance.FIFTY, setOf(SParam.S11), attenuation = 0).toList()
            }
        } finally { client.stop() }
    }

    @Test fun concurrentRequestsStayCorrelated() = runBlocking<Unit> {
        val link = Link()
        val client = SnaClient(link, this)
        link.respond = { link.send(it, "\"ok\":true,\"fw\":\"${it.getValue("id")}\",\"proto\":3,\"hw_rev\":1,\"freq_min_hz\":50000000,\"freq_max_hz\":6000000000") }
        client.start()
        try {
            val ids = withTimeout(5000) { (1..200).map { async(Dispatchers.Default) { client.ping().fw } }.awaitAll() }
            assertEquals(200, ids.toSet().size)
        } finally { client.stop() }
    }

    @Test fun twoPortExportNeverInventsMissingMeasurements() {
        val partial = SweepPoint(pt = 0, f = 50_000_000, s11_db = -10.0, s21_db = -20.0)
        assertFailsWith<IllegalArgumentException> { Touchstone.formatS2P(listOf(partial), 50) }
        val complete = partial.copy(s12_db = -30.0, s22_db = -40.0)
        assertTrue(Touchstone.formatS2P(listOf(complete), 75).contains("50000000 -10.0 0.0 -20.0 0.0 -30.0 0.0 -40.0 0.0"))
    }
}
