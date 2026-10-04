package io.github.ml3rc.sna.client.ui

import androidx.compose.ui.graphics.toAwtImage
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.serialization.json.*
import org.junit.Rule
import org.junit.Test
import java.io.File
import javax.imageio.ImageIO

class InstrumentUiTest {
    @get:Rule val compose = createComposeRule()

    @Test fun startupConnectsAndWorkspaceNavigationRemainsUsable() {
        val transport = PreviewTransport()
        val session = SnaViewModel(transport)
        try {
            compose.setContent { App(session) }
            compose.onNodeWithText("Scalar network analyzer").assertIsDisplayed()
            compose.waitUntil(5000) { compose.onAllNodesWithText("SNA analyzer").fetchSemanticsNodes().isNotEmpty() }
            capture("desktop-startup.png")
            compose.onNodeWithText("SNA analyzer").performClick()
            compose.waitUntil(5000) { compose.onAllNodesWithText("Start sweep").fetchSemanticsNodes().isNotEmpty() }
            compose.onNodeWithText("Start sweep").assertIsDisplayed()
            compose.onNodeWithText("S12").performClick().assertIsOn()
            compose.onNodeWithText("S12").performClick().assertIsOff()
            compose.onNodeWithText("Start sweep").performClick()
            compose.waitUntil(5000) { session.points.value.size == 201 && !session.sweeping.value }
            compose.onNodeWithText("Export JSON").assertIsEnabled()
            capture("desktop-sweep.png")
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
        } finally { session.close() }
    }

    private fun capture(name: String) {
        val directory = File("build/ui-previews").apply { mkdirs() }
        ImageIO.write(compose.onRoot().captureToImage().toAwtImage(), "png", File(directory, name))
    }

    private class PreviewTransport : SerialTransport {
        var opens = 0
        override var isOpen = false
        private val lines = Channel<String>(Channel.UNLIMITED)
        override suspend fun listPorts() = listOf(SerialPortInfo("preview", "Simulated SNA · USB"))
        override suspend fun open(port: SerialPortInfo) { opens++; isOpen = true }
        override fun close() { isOpen = false }
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            val request = Json.parseToJsonElement(line).jsonObject
            val id = request.getValue("id").jsonPrimitive.int
            if (request.getValue("cmd").jsonPrimitive.content == "sweep") {
                val count = request.getValue("points").jsonPrimitive.int
                val start = request.getValue("f0").jsonPrimitive.long
                val stop = request.getValue("f1").jsonPrimitive.long
                repeat(count) { index ->
                    val t = index.toDouble() / (count - 1).coerceAtLeast(1)
                    val frequency = start + ((stop - start) * t).toLong()
                    val distance = (t - .52) / .075
                    val dip = kotlin.math.exp(-distance * distance)
                    val transmission = -2.0 - 24.0 * dip - 1.2 * t
                    val reflection = -18.0 + 13.0 * dip + 1.3 * kotlin.math.sin(t * 18.0)
                    lines.send("""{"id":$id,"pt":$index,"f":$frequency,"s11_db":$reflection,"s21_db":$transmission,"locked":true,"cal":true}""")
                }
                lines.send("""{"id":$id,"ok":true,"done":true}""")
                return
            }
            val response = if (request.getValue("cmd").jsonPrimitive.content == "cal_get")
                """{"id":$id,"ok":false,"err":"not_calibrated"}"""
            else """{"id":$id,"ok":true,"fw":"preview","hw_rev":1,"proto":1}"""
            lines.send(response)
        }
    }
}
