package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.data.FileStore
import io.github.ml3rc.sna.client.protocol.DetectorPath
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import kotlinx.coroutines.launch

private fun DetectorPath.color(): Color = when (this) {
    DetectorPath.P1_FWD -> ChartColors.S11
    DetectorPath.P1_BWD -> ChartColors.S21
    DetectorPath.P2_FWD -> ChartColors.S12
    DetectorPath.P2_BWD -> ChartColors.S22
}

/** The normal sweep with the four detector coupler states as the "parameters" instead of S11/S21/S12/S22. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun RawSweepScreen(session: Session, compactUi: Boolean = false) {
    val raw = session.rawSweep
    val settings by raw.settings.collectAsState()
    val points by raw.points.collectAsState()
    val running by raw.running.collectAsState()
    val error by raw.error.collectAsState()
    val measured by raw.measured.collectAsState()
    val sweepRunning by session.sweep.running.collectAsState()
    val scope = rememberCoroutineScope()
    val toolGate = rememberGate(session, Tool.SWEEP)
    val gate = ToolGate(toolGate.reason ?: if (sweepRunning) "An S-parameter sweep is running." else null, running)
    val editable = gate.idleEditable
    val shown = measured?.second ?: settings.paths
    val used = DetectorPath.entries.filter { it in shown }

    ScreenColumn("Raw detector sweep", compactUi, "Same measurement as the S-parameter sweep, but each point returns the detector " +
        "reading of the selected coupler states, with no reference division and no calibration.") {
        Panel(title = "Acquisition", modifier = Modifier.fillMaxWidth()) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumberField(settings.f0, { v -> raw.update { it.copy(f0 = v) } }, "Start (Hz)", Modifier.weight(1f), editable)
                NumberField(settings.f1, { v -> raw.update { it.copy(f1 = v) } }, "Stop (Hz)", Modifier.weight(1f), editable)
                if (!compactUi) NumberField(settings.points, { v -> raw.update { it.copy(points = v) } }, "Points", Modifier.weight(1f), editable)
            }
            if (compactUi) {
                Spacer(Modifier.height(4.dp))
                NumberField(settings.points, { v -> raw.update { it.copy(points = v) } }, "Points", Modifier.fillMaxWidth(), editable)
            }
            Spacer(Modifier.height(8.dp))
            Text("Source", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("Port 1", settings.port == 1, editable) { raw.update { it.copy(port = 1) } }
                ChoiceChip("Port 2", settings.port == 2, editable) { raw.update { it.copy(port = 2) } }
                ImpedanceChoice(settings.z, editable) { z -> raw.update { it.copy(z = z) } }
                SpacingChoice(settings.log, editable) { log -> raw.update { it.copy(log = log) } }
            }
            Spacer(Modifier.height(8.dp))
            Text("Detector states", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                DetectorPath.entries.forEach { path ->
                    ToggleChip(path.label, path in settings.paths, { on ->
                        raw.update { it.copy(paths = if (on) it.paths + path else it.paths - path) }
                    }, enabled = editable, accent = path.color())
                }
            }
            Spacer(Modifier.height(8.dp))
            LevelSlider(settings.attenSteps, { v -> raw.update { it.copy(attenSteps = v) } }, Modifier.testTag("raw-atten"),
                enabled = gate.liveEditable, onCommit = { raw.setLevel(raw.settings.value.attenSteps) })
            Hint("Level relative to maximum output; during a sweep it changes the running sweep from its next point on, " +
                "and like the impedance it stays the device's setting afterwards. " + OUTPUT_LEVEL_NOTE)
            Spacer(Modifier.height(8.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(enabled = editable && settings.valid, onClick = { raw.start() }) {
                    Text(if (running) "Sweeping…" else "Start raw sweep")
                }
                OutlinedButton(enabled = running, onClick = { raw.abort() }) { Text("Abort") }
                OutlinedButton(enabled = !running && points.any { p -> used.any { p.detectorRaw(it) != null } }, onClick = {
                    scope.launch { FileStore.save("raw-detector-sweep.csv", raw.toCsv()) }
                }) { Text("Export CSV") }
            }
            ErrorText(gate.reason, "raw-blocked")
            ErrorText(error, "raw-error")
            val unlocked = points.count { it.measured && !it.locked }
            if (unlocked > 0) ErrorText("$unlocked points measured without confirmed source lock")
        }

        Spacer(Modifier.height(8.dp))
        Panel(title = "Detector readings", modifier = Modifier.fillMaxWidth()) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("dBm at detector", !settings.showCounts) { raw.update { it.copy(showCounts = false) } }
                ChoiceChip("ADC counts", settings.showCounts) { raw.update { it.copy(showCounts = true) } }
            }
            Spacer(Modifier.height(8.dp))
            SnaChart(
                series = used.map { it.label to it.color() },
                ySeries = used.map { path -> points.map { p -> if (settings.showCounts) p.detectorRaw(path)?.toDouble() else p.detectorDbm(path) } },
                xLabelFormatter = { idx -> points.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" },
                yLabelFormatter = { if (settings.showCounts) "%.0f".format(it) else "%.1f".format(it) },
                modifier = Modifier.fillMaxWidth().height(if (compactUi) 280.dp else 380.dp).testTag("raw-chart"),
            )
            Spacer(Modifier.height(6.dp))
            Text("dBm is detector-referred and uncalibrated (datasheet slope and intercept). The detector bottoms out around " +
                "-60 dBm (about 2.11 V, 2620 counts); flat traces at that value mean no signal. A higher ADC count means less power.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}
