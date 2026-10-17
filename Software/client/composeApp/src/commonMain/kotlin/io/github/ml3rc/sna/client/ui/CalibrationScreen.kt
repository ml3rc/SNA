package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.protocol.CAL_CAPTURE_ATTEN
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.session.CalModeWizardState
import io.github.ml3rc.sna.client.session.CalStep
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool

/** The SLOT calibration wizard. Its state lives in the session, so a capture keeps running on other tabs. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CalibrationScreen(session: Session, compactUi: Boolean = false) {
    val wizard = session.calibration
    LaunchedEffect(wizard) { wizard.ensureDetected() }
    val gate = rememberGate(session, Tool.CALIBRATION)

    val f0 by wizard.f0.collectAsState()
    val f1 by wizard.f1.collectAsState()
    val points by wizard.points.collectAsState()
    val gridLocked by wizard.gridLocked.collectAsState()
    val detecting by wizard.detecting.collectAsState()
    val wizardError by wizard.error.collectAsState()
    val capturing by wizard.capturing.collectAsState()
    val editable = gate.idleEditable

    var confirmingClear by remember { mutableStateOf(false) }
    var selected by rememberSaveable { mutableStateOf(Impedance.FIFTY.name) }
    val impedance = Impedance.valueOf(selected)
    val modeState = if (impedance == Impedance.FIFTY) wizard.mode50 else wizard.mode75

    ScreenColumn("Calibration (SLOT)", compactUi,
        "Scalar calibration: Open+Short average for S11/S22 tracking, Thru directly for S21/S12 tracking.") {
        Panel(title = "Frequency grid", modifier = Modifier.fillMaxWidth()) {
            Hint("Supported range: 50 MHz–6 GHz. Captures are linear; the device interpolates onto any sweep, also logarithmic ones.")
            if (detecting) Hint("Checking device for an existing calibration grid…")
            else if (gridLocked) Hint("Locked to the device's existing calibration grid (clear all to change it).")
            Spacer(Modifier.height(8.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                NumberField(f0, wizard::setF0, "Start (Hz)", Modifier.weight(1f), editable && !gridLocked)
                NumberField(f1, wizard::setF1, "Stop (Hz)", Modifier.weight(1f), editable && !gridLocked)
                if (!compactUi) NumberField(points, wizard::setPoints, "Points", Modifier.weight(1f), editable && !gridLocked)
            }
            if (compactUi) {
                Spacer(Modifier.height(8.dp))
                NumberField(points, wizard::setPoints, "Points", Modifier.fillMaxWidth(), editable && !gridLocked)
            }
            Spacer(Modifier.height(8.dp))
            if (!confirmingClear) {
                TextButton(enabled = editable, onClick = { confirmingClear = true }) {
                    Text("Clear ALL calibration & start a new grid", color = MaterialTheme.colorScheme.error.copy(alpha = if (editable) 1f else 0.45f))
                }
            } else {
                Column {
                    Text("This wipes all 8 calibration sets on both impedance modes. Are you sure?")
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(enabled = editable, onClick = { wizard.clearAllAndUnlock(); confirmingClear = false }) { Text("Yes, clear all") }
                        TextButton(onClick = { confirmingClear = false }) { Text("Cancel") }
                    }
                }
            }
            if (!detecting && wizardError != null) TextButton(enabled = editable, onClick = { wizard.detectExistingGrid() }) { Text("Read the grid again") }
            ErrorText(wizardError)
        }

        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            // Switching the shown impedance is display only; each mode keeps its own progress.
            Impedance.entries.forEach { z -> ChoiceChip("${z.ohms} Ω", impedance == z) { selected = z.name } }
            if (capturing) OutlinedButton(onClick = { wizard.abortCapture() }) { Text("Abort capture") }
        }
        Spacer(Modifier.height(8.dp))
        ErrorText(gate.reason, "calibration-blocked")
        CalModePanel(modeState, f0, f1, points, !detecting && wizardError == null, editable)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CalModePanel(mode: CalModeWizardState, f0Text: String, f1Text: String, pointsText: String, gridReady: Boolean, editable: Boolean) {
    val captures by mode.captures.collectAsState()
    val measuringStep by mode.measuringStep.collectAsState()
    val started by mode.started.collectAsState()
    val uploading by mode.uploading.collectAsState()
    val uploaded by mode.uploaded.collectAsState()
    val verifying by mode.verifying.collectAsState()
    val verifyResult by mode.verifyResult.collectAsState()
    val error by mode.error.collectAsState()

    val f0Hz = f0Text.trim().toLongOrNull()
    val f1Hz = f1Text.trim().toLongOrNull()
    val n = pointsText.trim().toIntOrNull()
    val gridValid = gridReady && f0Hz != null && f1Hz != null && n != null &&
        FrequencyLimits.isValidRange(f0Hz, f1Hz) && n in ProtocolLimits.CAL_POINTS

    Panel(title = "Calibration steps — ${mode.impedance.ohms} Ω", modifier = Modifier.fillMaxWidth()) {
        Hint("Capture and verification use maximum source level (${formatAtten(CAL_CAPTURE_ATTEN)} attenuation). " +
            "At most ${ProtocolLimits.CAL_POINTS_MAX} points.")
        Spacer(Modifier.height(6.dp))
        OutlinedButton(onClick = { mode.beginOrRestart() }, enabled = editable && gridValid && !uploading) {
            Text(if (started) "Restart calibration for this mode" else "Begin calibration for this mode")
        }
        Spacer(Modifier.height(6.dp))

        CalStep.entries.forEach { step ->
            val capturedCount = captures[step]?.size
            Row {
                Column(Modifier.weight(1f).padding(vertical = 6.dp)) {
                    Text(step.label, style = MaterialTheme.typography.titleSmall)
                    Text(capturedCount?.let { "$it pts captured" } ?: "Not captured",
                        style = MaterialTheme.typography.bodySmall,
                        color = if (capturedCount != null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                }
                OutlinedButton(enabled = editable && started && gridValid, onClick = { mode.measure(step, f0Hz!!, f1Hz!!, n!!) }) {
                    Text(if (measuringStep == step) "Measuring…" else "Measure")
                }
            }
            Spacer(Modifier.height(4.dp))
        }

        ErrorText(error)

        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = editable && mode.allCaptured && !uploading && gridValid, onClick = { mode.computeAndUpload(f0Hz!!, f1Hz!!) }) {
                Text(if (uploading) "Uploading…" else "Compute & Upload")
            }
            OutlinedButton(enabled = editable && uploaded && gridValid, onClick = { mode.verify(f0Hz!!, f1Hz!!, n!!) }) {
                Text(if (verifying) "Verifying…" else "Verify")
            }
        }
        if (uploaded) {
            Spacer(Modifier.height(4.dp))
            Text("Uploaded.", color = MaterialTheme.colorScheme.primary)
        }
        verifyResult?.let {
            Spacer(Modifier.height(4.dp))
            Text(if (it) "Verify: all points report cal=true" else "Verify: some points are NOT calibrated",
                color = if (it) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
        }
    }

    Spacer(Modifier.height(8.dp))
    // Each captured step adds its traces; labels/colors and data are built together so they stay aligned.
    val traces = buildList<Triple<String, Color, List<Double?>>> {
        captures[CalStep.OPEN_PORT1]?.takeIf { it.isNotEmpty() }?.let { add(Triple("Open1 (S11)", ChartColors.CalOpen1, it.map { p -> p.s11_db })) }
        captures[CalStep.SHORT_PORT1]?.takeIf { it.isNotEmpty() }?.let { add(Triple("Short1 (S11)", ChartColors.CalShort1, it.map { p -> p.s11_db })) }
        captures[CalStep.OPEN_PORT2]?.takeIf { it.isNotEmpty() }?.let { add(Triple("Open2 (S22)", ChartColors.CalOpen2, it.map { p -> p.s22_db })) }
        captures[CalStep.SHORT_PORT2]?.takeIf { it.isNotEmpty() }?.let { add(Triple("Short2 (S22)", ChartColors.CalShort2, it.map { p -> p.s22_db })) }
        captures[CalStep.THRU]?.takeIf { it.isNotEmpty() }?.let { thru ->
            add(Triple("Thru (S21)", ChartColors.CalThruS21, thru.map { p -> p.s21_db }))
            add(Triple("Thru (S12)", ChartColors.CalThruS12, thru.map { p -> p.s12_db }))
        }
    }
    // All captures of a mode share the one grid, so any of them labels the axis.
    val freqs = CalStep.entries.firstNotNullOfOrNull { captures[it]?.takeIf { p -> p.isNotEmpty() } }
    Panel(title = "Captured curves preview") {
        SnaChart(
            series = traces.map { it.first to it.second },
            ySeries = traces.map { it.third },
            xLabelFormatter = { idx -> freqs?.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" },
            modifier = Modifier.fillMaxWidth().height(300.dp),
        )
    }
}
