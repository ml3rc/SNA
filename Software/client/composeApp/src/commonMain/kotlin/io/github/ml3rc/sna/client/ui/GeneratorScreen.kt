package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProbeResult
import io.github.ml3rc.sna.client.session.COUNTS_PER_DB
import io.github.ml3rc.sna.client.session.GeneratorOutput
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.session.diagnoseProbe
import io.github.ml3rc.sna.client.session.formatDetectorDbm
import io.github.ml3rc.sna.client.session.parseFrequencyMhz

/**
 * CW signal generator: pick port, impedance, relative level and frequency, then switch it on/off.
 * Everything here acts on the device while the output is on (level and impedance live, port and
 * frequency through "Apply changes"), so the controls only lock while another tool runs.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun GeneratorScreen(session: Session, compactUi: Boolean = false) {
    val generator = session.generator
    val active by generator.active.collectAsState()
    val busy by generator.busy.collectAsState()
    val error by generator.error.collectAsState()
    val gate = rememberGate(session, Tool.GENERATOR)
    val editable = gate.liveEditable

    // Start from what the device is doing, so reopening the tab while it is on shows the real settings.
    var port by remember { mutableStateOf(active?.port ?: 1) }
    var z by remember { mutableStateOf(active?.z ?: Impedance.FIFTY) }
    var freqMhz by remember { mutableStateOf(active?.freqHz?.let { "%.6f".format(it / 1e6).trimEnd('0').trimEnd('.', ',') } ?: "1000") }
    var attenSteps by remember { mutableStateOf(active?.attenSteps ?: 64) }

    val freqHz = parseFrequencyMhz(freqMhz)
    val requested = freqHz?.let { GeneratorOutput(port, z, attenSteps, it) }
    val on = active != null
    // Level and impedance change a running output live; only port and frequency need a restart.
    val changed = on && requested != null && (requested.port != active?.port || requested.freqHz != active?.freqHz)

    ScreenColumn("Signal generator", compactUi) {
        Panel(title = "Output", modifier = Modifier.fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.semantics(mergeDescendants = true) {}.testTag("generator-status")) {
                Box(Modifier.size(14.dp).background(if (on) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline))
                Text(if (on) "ON" else "OFF", style = MaterialTheme.typography.titleLarge, fontFamily = FontFamily.Monospace)
                active?.let {
                    Text("${it.freqHz?.let(::formatFrequency) ?: "unknown frequency"} · Port ${it.port} · ${it.z.ohms} Ω · ${formatAtten(it.attenSteps)}",
                        color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
                }
            }
            Spacer(Modifier.height(10.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("Port 1", port == 1, editable) { port = 1 }
                ChoiceChip("Port 2", port == 2, editable) { port = 2 }
                ImpedanceChoice(z, editable) { choice -> z = choice; if (on) generator.setImpedance(choice) }
            }
            Spacer(Modifier.height(8.dp))
            NumberField(freqMhz, { freqMhz = it }, "Frequency (MHz)", Modifier.fillMaxWidth(), editable, decimal = true)
            Text("Supported range: 50–6000 MHz", style = MaterialTheme.typography.labelSmall,
                color = if (freqHz == null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.height(8.dp))
            LevelSlider(attenSteps, { attenSteps = it }, Modifier.testTag("generator-atten"), enabled = editable,
                onCommit = { if (on) generator.setLevel(attenSteps) })
            Hint("Attenuation relative to maximum output (0.25 dB per step), not calibrated absolute power. " +
                "While the output is on, the slider changes the level and the 50/75 Ω choice the impedance live. " +
                OUTPUT_LEVEL_NOTE)
            Spacer(Modifier.height(10.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(enabled = requested != null && !busy && editable && (!on || changed),
                    onClick = { requested?.let(generator::start) }) {
                    Text(if (!on) "Turn on" else "Apply changes")
                }
                OutlinedButton(enabled = on && !busy && editable, onClick = { generator.stop() }) { Text("Turn off") }
            }
            if (changed) Hint("Settings differ from the running output. Applying briefly interrupts it.")
            ErrorText(gate.reason, "generator-blocked")
            ErrorText(error, "generator-error")
        }
        Spacer(Modifier.height(8.dp))
        ProbePanel(session, on, editable)
        Spacer(Modifier.height(6.dp))
        Text("While the generator is on, sweeps, calibration and the power meter are locked out. The output also turns off " +
            "when the USB connection ends.",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

/**
 * Troubleshooting readout: with the route set above (generator on, or terminated), shows what the
 * detector sees on each coupler path and the S-parameter pair derived from them, raw and calibrated.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProbePanel(session: Session, generatorOn: Boolean, enabled: Boolean) {
    val probe = session.probe
    val running by probe.running.collectAsState()
    val result by probe.result.collectAsState()
    val error by probe.error.collectAsState()

    Panel(title = "Detector probe", modifier = Modifier.fillMaxWidth()) {
        Hint(if (generatorOn) "Reads the detector on all four coupler paths with the source on as set above."
            else "Generator off: the output is terminated, so the readings show leakage and the noise floor.")
        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = !running && enabled, onClick = { probe.start() }) { Text("Start live readout") }
            OutlinedButton(enabled = running, onClick = { probe.stop() }) { Text("Stop") }
        }
        ErrorText(error, "probe-error")
        result?.let { ProbeTable(it, showHints = true) }
    }
}

/** Per-path detector readings, the derived S-parameter pair and plain-language hints. Shared with manual control. */
@Composable
internal fun ProbeTable(r: ProbeResult, showHints: Boolean) {
    val mono = FontFamily.Monospace
    val dim = MaterialTheme.colorScheme.onSurfaceVariant
    Spacer(Modifier.height(10.dp))
    Column(Modifier.semantics(mergeDescendants = true) {}.testTag("probe-table")) {
        Row(Modifier.fillMaxWidth()) {
            listOf("Path" to 1.3f, "Detector" to 1.2f, "Raw" to 0.8f, "Spread" to 0.9f, "Range" to 0.7f).forEach { (title, w) ->
                Text(title, Modifier.weight(w), style = MaterialTheme.typography.labelMedium, color = dim)
            }
        }
        r.readings.forEach { x ->
            val spread = (x.max - x.min) / COUNTS_PER_DB
            val bad = x.range != "ok"
            Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                Text(x.name, Modifier.weight(1.3f), fontFamily = mono)
                Text(formatDetectorDbm(x.dbm, x.range), Modifier.weight(1.2f), fontFamily = mono,
                    color = if (bad) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface)
                Text("${x.raw}", Modifier.weight(0.8f), fontFamily = mono)
                Text("%.1f dB".format(spread), Modifier.weight(0.9f), fontFamily = mono,
                    color = if (spread > 2.0) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface)
                Text(x.range, Modifier.weight(0.7f), fontFamily = mono,
                    color = if (bad) MaterialTheme.colorScheme.error else dim)
            }
        }
    }
    r.derived?.let { d ->
        Spacer(Modifier.height(10.dp))
        Column(Modifier.semantics(mergeDescendants = true) {}.testTag("probe-derived")) {
            listOf(Triple(d.refl, d.refl_db, d.refl_cal_db), Triple(d.trans, d.trans_db, d.trans_cal_db)).forEach { (name, raw, cal) ->
                Row(Modifier.fillMaxWidth()) {
                    Text(name.uppercase(), Modifier.weight(1f), style = MaterialTheme.typography.titleSmall)
                    Text("raw %.2f dB".format(raw), Modifier.weight(1.4f), fontFamily = mono)
                    Text(if (d.cal) "calibrated %.2f dB".format(cal) else "not calibrated", Modifier.weight(1.8f), fontFamily = mono,
                        color = if (d.cal && cal > 0.5) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurface)
                }
            }
        }
    }
    val hints = if (showHints) diagnoseProbe(r) else emptyList()
    if (hints.isNotEmpty()) {
        Spacer(Modifier.height(10.dp))
        Column(Modifier.semantics(mergeDescendants = true) {}.testTag("probe-hints"), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            hints.forEach { Text("• $it", style = MaterialTheme.typography.bodySmall) }
        }
    }
}
