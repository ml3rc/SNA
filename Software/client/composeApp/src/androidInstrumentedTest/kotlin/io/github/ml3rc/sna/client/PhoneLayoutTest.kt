package io.github.ml3rc.sna.client

import android.content.pm.ActivityInfo
import android.content.res.Configuration
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.assertIsOn
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.lifecycle.ViewModelProvider
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import io.github.ml3rc.sna.client.ui.App
import io.github.ml3rc.sna.client.ui.SnaViewModel
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class PhoneLayoutTest {
    @get:Rule val compose = createComposeRule()

    @Test fun startupShowsConnectionScreenWithoutOpeningTransport() {
        val transport = PhoneTransport()
        val session = SnaViewModel(transport)
        try {
            compose.setContent { Box(Modifier.size(360.dp, 640.dp)) { App(session, compactUi = true) } }
            compose.onNodeWithText("Scalar network analyzer").assertIsDisplayed()
            saveScreenshot("phone-startup.png")
            compose.onNodeWithText("Rescan USB").performScrollTo().performClick()
            compose.onNodeWithText("No SNA devices found").assertIsDisplayed()
            org.junit.Assert.assertEquals(0, transport.opens)
        } finally { session.close() }
    }

    @Test fun usbSessionOwnerSurvivesActivityRecreation() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            lateinit var original: SnaViewModel
            scenario.onActivity { original = ViewModelProvider(it)[SnaSessionViewModel::class.java].session }
            repeat(2) {
                scenario.recreate()
                scenario.onActivity { assertSame(original, ViewModelProvider(it)[SnaSessionViewModel::class.java].session) }
            }
            listOf(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE to Configuration.ORIENTATION_LANDSCAPE,
                ActivityInfo.SCREEN_ORIENTATION_PORTRAIT to Configuration.ORIENTATION_PORTRAIT).forEach { (request, expected) ->
                scenario.onActivity { it.requestedOrientation = request }
                compose.waitUntil(5000) {
                    var matches = false
                    scenario.onActivity { matches = it.resources.configuration.orientation == expected }
                    matches
                }
                scenario.onActivity { assertSame(original, ViewModelProvider(it)[SnaSessionViewModel::class.java].session) }
            }
        }
    }

    @Test fun phoneSweepScrollsAndRestoresSettingsWithoutReopeningTransport() {
        val transport = PhoneTransport()
        val session = SnaViewModel(transport)
        try {
            session.connect(SerialPortInfo("test", "Simulated SNA")) {}
            compose.waitUntil(5000) { session.connected.value && !session.connecting.value }
            val restoration = StateRestorationTester(compose)
            restoration.setContent {
                Box(Modifier.size(360.dp, 640.dp)) { App(session, compactUi = true) }
            }
            compose.onNodeWithText("Start (Hz)").performTextReplacement("100000000")
            restoration.emulateSavedInstanceStateRestore()
            compose.onNodeWithText("100000000").assertIsDisplayed()
            compose.onNodeWithText("Start sweep").performScrollTo().performClick()
            compose.waitUntil(5000) { session.points.value.size == 201 && !session.sweeping.value }
            compose.onNodeWithText("100000000").performScrollTo()
            saveScreenshot("phone-sweep-controls.png")
            compose.onNodeWithTag("sweep-results").performScrollTo().assertIsDisplayed()
            saveScreenshot("phone-sweep-chart.png")
            compose.onNodeWithText("Start sweep").performScrollTo().assertIsDisplayed()
            compose.onNodeWithText("S12").performScrollTo().performClick().assertIsOn()
            compose.onNodeWithText("Calibrate").performClick()
            compose.onNodeWithText("Points").performScrollTo().assertIsDisplayed()
            saveScreenshot("phone-calibration.png")
            compose.onNodeWithText("SDI").performClick()
            compose.onNodeWithText("Add row").performClick()
            compose.onNodeWithText("f (Hz)").assertIsDisplayed()
            compose.onNodeWithText("thresh (dB)").assertIsDisplayed()
            saveScreenshot("phone-self-test.png")
            compose.onNodeWithText("Remove").performClick()
            compose.onNodeWithText("System").performClick()
            compose.onNodeWithText("Housekeeping").assertIsDisplayed()
            saveScreenshot("phone-diagnostics.png")
            assertTrue(session.connected.value)
            org.junit.Assert.assertEquals(1, transport.opens)
        } finally {
            session.close()
        }
    }

    private fun saveScreenshot(name: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        File(context.getExternalFilesDir(null), name).outputStream().use {
            compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it)
        }
    }

    private class PhoneTransport : SerialTransport {
        var opens = 0
        override var isOpen = false
        private val lines = Channel<String>(Channel.UNLIMITED)
        override suspend fun listPorts() = emptyList<SerialPortInfo>()
        override suspend fun open(port: SerialPortInfo) { opens++; isOpen = true }
        override fun close() { isOpen = false }
        override fun incomingLines() = lines.receiveAsFlow()
        override suspend fun writeLine(line: String) {
            val request = Json.parseToJsonElement(line).jsonObject
            val id = request.getValue("id").jsonPrimitive.int
            if (request.getValue("cmd").jsonPrimitive.content == "sweep") {
                val count = request.getValue("points").jsonPrimitive.int
                val start = request.getValue("f0").jsonPrimitive.content.toLong()
                val stop = request.getValue("f1").jsonPrimitive.content.toLong()
                repeat(count) { index ->
                    val t = index.toDouble() / (count - 1).coerceAtLeast(1)
                    val frequency = start + ((stop - start) * t).toLong()
                    val distance = (t - .52) / .075
                    val dip = kotlin.math.exp(-distance * distance)
                    lines.send("""{"id":$id,"pt":$index,"f":$frequency,"s11_db":${-18 + 13 * dip},"s21_db":${-2 - 24 * dip},"locked":true,"cal":true}""")
                }
                lines.send("""{"id":$id,"ok":true,"done":true}""")
                return
            }
            lines.send(if (request.getValue("cmd").jsonPrimitive.content == "cal_get")
                """{"id":$id,"ok":false,"err":"not_calibrated"}"""
            else """{"id":$id,"ok":true,"fw":"test","hw_rev":1,"proto":1}""")
        }
    }
}
