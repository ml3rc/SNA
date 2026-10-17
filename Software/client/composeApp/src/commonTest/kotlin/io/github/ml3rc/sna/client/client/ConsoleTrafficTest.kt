package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ConsoleTrafficTest {
    @Test
    fun logStaysBoundedAndKeepsTheNewestLines() {
        val log = TrafficLog(capacity = 3)
        repeat(10) { log.add(TrafficDirection.RX, TrafficKind.COMMAND, "line $it") }
        assertEquals(listOf("line 7", "line 8", "line 9"), log.snapshot().map { it.text })
        assertEquals(listOf(8L, 9L, 10L), log.snapshot().map { it.seq })
        log.clear()
        assertTrue(log.snapshot().isEmpty())
    }

    /** Echoes every request's id back with ok. */
    private class EchoDevice : SerialTransport {
        val lines = Channel<String>(Channel.UNLIMITED)
        val sent = mutableListOf<String>()
        override val isOpen = true
        override suspend fun listPorts() = emptyList<SerialPortInfo>()
        override suspend fun open(port: SerialPortInfo) {}
        override fun close() {}
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            sent += line
            val id = runCatching { Json.parseToJsonElement(line).jsonObject["id"] }.getOrNull() ?: return
            lines.send("""{"id":$id,"ok":true,"fw":"t","proto":3,"hw_rev":1,"z":50}""")
        }
    }

    @Test
    fun consoleLinesGetTheirOwnIdsAndEveryLineIsLoggedByKind() = runBlocking {
        val device = EchoDevice()
        val client = SnaClient(device, this).also { it.start() }
        try {
            val line = client.sendRaw("""  {"cmd":"ping"}  """)
            assertEquals("console-1", Json.parseToJsonElement(line).jsonObject.getValue("id").jsonPrimitive.content)
            assertEquals("not json", client.sendRaw("not json"))
            client.status() // a poll
            client.ping()
            withTimeout(3000) { while (client.traffic.snapshot().count { it.direction == TrafficDirection.RX } < 3) delay(10) }
            val entries = client.traffic.snapshot()
            assertEquals(listOf(TrafficKind.CONSOLE, TrafficKind.CONSOLE), entries.filter { it.direction == TrafficDirection.TX }.take(2).map { it.kind })
            assertTrue(entries.any { it.direction == TrafficDirection.RX && it.kind == TrafficKind.CONSOLE && "console-1" in it.text })
            assertTrue(entries.any { it.direction == TrafficDirection.RX && it.kind == TrafficKind.POLL })
            assertTrue(entries.any { it.direction == TrafficDirection.RX && it.kind == TrafficKind.COMMAND })
            // A console reply never completes one of the client's own requests.
            assertEquals(4, device.sent.size)
        } finally {
            client.stop(); device.lines.close()
        }
    }
}
