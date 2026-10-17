package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.session.formatDetectorDbm

/** Power meter: continuously reads the signal arriving at one port. Changing port or impedance restarts a running reading. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun PowerMeterScreen(session: Session, compactUi: Boolean = false) {
    val meter = session.power
    val reading by meter.reading.collectAsState()
    val error by meter.error.collectAsState()
    val on by meter.running.collectAsState()
    val port by meter.port.collectAsState()
    val z by meter.z.collectAsState()
    val gate = rememberGate(session, Tool.POWER)

    ScreenColumn("Port power", compactUi) {
        Panel(title = "Input", modifier = Modifier.fillMaxWidth()) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("Port 1", port == 1, gate.liveEditable) { meter.select(1, z) }
                ChoiceChip("Port 2", port == 2, gate.liveEditable) { meter.select(2, z) }
                ImpedanceChoice(z, gate.liveEditable) { meter.select(port, it) }
            }
            Spacer(Modifier.height(10.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(enabled = !on && !gate.locked, onClick = { meter.start() }) { Text("Start reading") }
                OutlinedButton(enabled = on, onClick = { meter.stop() }) { Text("Stop reading") }
            }
            ErrorText(gate.reason, "power-blocked")
            ErrorText(error, "power-error")
        }
        Spacer(Modifier.height(8.dp))

        Panel(title = if (on) "Reading · Port $port" else "Reading (off)", modifier = Modifier.fillMaxWidth()) {
            val shown = if (on) reading else null
            Text(shown?.let { formatDetectorDbm(it.dbm, it.range) } ?: "— dBm",
                style = MaterialTheme.typography.headlineLarge, fontFamily = FontFamily.Monospace,
                modifier = Modifier.testTag("power-reading"))
            Text(shown?.let { "ADC ${it.raw} · ${"%.3f".format(it.volts)} V" } ?: "ADC —",
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            when (shown?.range) {
                "low" -> ErrorText("Below the detector range (about -55 dBm): the value is not reliable.", "power-range")
                "high" -> ErrorText("Above the detector range (about 0 dBm): the value is clipped. Reduce the input; maximum is +10 dBm.", "power-range")
            }
        }
        Spacer(Modifier.height(6.dp))
        Text("Approximate and uncalibrated: referenced to the detector input from datasheet values, so coupler " +
            "coupling, 75 Ω path loss and frequency dependence are not included. Use it to compare levels, " +
            "not as an absolute reference. Maximum input is +10 dBm. The generator must be off.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
