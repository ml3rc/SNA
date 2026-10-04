package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.FrequencyLimits

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun CalibrationScreen(viewModel: SnaViewModel, compactUi: Boolean = false) {
    val client = viewModel.client ?: return
    val coroutineScope = rememberCoroutineScope()
    val wizard = remember(client) { CalibrationWizardState(client, coroutineScope) }

    LaunchedEffect(client) { wizard.detectExistingGrid() }

    val f0 by wizard.f0.collectAsState()
    val f1 by wizard.f1.collectAsState()
    val points by wizard.points.collectAsState()
    val gridLocked by wizard.gridLocked.collectAsState()
    val detecting by wizard.detecting.collectAsState()
    val wizardError by wizard.error.collectAsState()

    var confirmingClear by remember { mutableStateOf(false) }
    var selectedImpedance by remember { mutableStateOf(Impedance.FIFTY) }
    val modeState = if (selectedImpedance == Impedance.FIFTY) wizard.mode50 else wizard.mode75

    Column(Modifier.fillMaxSize().padding(14.dp).verticalScroll(rememberScrollState())) {
        Text("Calibration (SLOT)", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(4.dp))
        Text(
            "Scalar calibration: Open+Short average for S11/S22 tracking, Thru directly for S21/S12 tracking.",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))

        Panel(title = "Frequency grid", modifier = Modifier.fillMaxWidth()) {
            Text("Supported range: 50 MHz–6 GHz")
            if (detecting) {
                Text("Checking device for an existing calibration grid…", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(8.dp))
            } else if (gridLocked) {
                Text("Locked to the device's existing calibration grid (clear all to change it).", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(8.dp))
            }
            Row {
                OutlinedTextField(value = f0, onValueChange = { wizard.setF0(it) }, enabled = !gridLocked, label = { Text("Start (Hz)") }, modifier = Modifier.weight(1f))
                Spacer(Modifier.width(8.dp))
                OutlinedTextField(value = f1, onValueChange = { wizard.setF1(it) }, enabled = !gridLocked, label = { Text("Stop (Hz)") }, modifier = Modifier.weight(1f))
                if (!compactUi) {
                    Spacer(Modifier.width(8.dp))
                    OutlinedTextField(value = points, onValueChange = { wizard.setPoints(it) }, enabled = !gridLocked, label = { Text("points") }, modifier = Modifier.weight(1f))
                }
            }
            if (compactUi) {
                Spacer(Modifier.height(8.dp))
                OutlinedTextField(value = points, onValueChange = { wizard.setPoints(it) }, enabled = !gridLocked,
                    label = { Text("Points") }, modifier = Modifier.fillMaxWidth())
            }
            Spacer(Modifier.height(8.dp))
            if (!confirmingClear) {
                TextButton(onClick = { confirmingClear = true }) { Text("Clear ALL calibration & start a new grid", color = MaterialTheme.colorScheme.error) }
            } else {
                Column {
                    Text("This wipes all 8 calibration sets on both impedance modes. Are you sure?")
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Button(onClick = { wizard.clearAllAndUnlock(); confirmingClear = false }) { Text("Yes, clear all") }
                        TextButton(onClick = { confirmingClear = false }) { Text("Cancel") }
                    }
                }
            }
            wizardError?.let { Spacer(Modifier.height(8.dp)); Text(it, color = MaterialTheme.colorScheme.error) }
        }

        Spacer(Modifier.height(8.dp))
        Row {
            ChoiceChip("50Ω", selectedImpedance == Impedance.FIFTY) { selectedImpedance = Impedance.FIFTY }
            Spacer(Modifier.width(8.dp))
            ChoiceChip("75Ω", selectedImpedance == Impedance.SEVENTY_FIVE) { selectedImpedance = Impedance.SEVENTY_FIVE }
        }
        Spacer(Modifier.height(8.dp))

        CalModePanel(modeState, f0, f1, points, !detecting && wizardError == null)
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CalModePanel(mode: CalModeWizardState, f0Text: String, f1Text: String, pointsText: String, gridReady: Boolean) {
    val captures by mode.captures.collectAsState()
    val measuringStep by mode.measuringStep.collectAsState()
    val started by mode.started.collectAsState()
    val uploading by mode.uploading.collectAsState()
    val uploaded by mode.uploaded.collectAsState()
    val verifyResult by mode.verifyResult.collectAsState()
    val error by mode.error.collectAsState()

    val f0Hz = f0Text.toLongOrNull()
    val f1Hz = f1Text.toLongOrNull()
    val n = pointsText.toIntOrNull()
    val gridValid = gridReady && f0Hz != null && f1Hz != null && n != null &&
        FrequencyLimits.isValidRange(f0Hz, f1Hz) && n in 1..240

    Panel(title = "Calibration steps — ${mode.impedance.ohms}Ω", modifier = Modifier.fillMaxWidth()) {
        Text("Capture and verification use maximum source level (0 dB attenuation). Previous level is restored afterward.")
        OutlinedButton(onClick = { mode.beginOrRestart() }, enabled = gridValid && !uploading && measuringStep == null) {
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
                OutlinedButton(
                    enabled = started && gridValid && measuringStep == null,
                    onClick = { mode.measure(step, f0Hz!!, f1Hz!!, n!!) },
                ) { Text(if (measuringStep == step) "Measuring…" else "Measure") }
            }
            Spacer(Modifier.height(4.dp))
        }

        error?.let { Spacer(Modifier.height(4.dp)); Text(it, color = MaterialTheme.colorScheme.error) }

        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = mode.allCaptured && !uploading && gridValid, onClick = { mode.computeAndUpload(f0Hz!!, f1Hz!!) }) {
                Text(if (uploading) "Uploading…" else "Compute & Upload")
            }
            OutlinedButton(enabled = uploaded && gridValid, onClick = { mode.verify(f0Hz!!, f1Hz!!, n!!) }) { Text("Verify") }
        }
        if (uploaded) {
            Spacer(Modifier.height(4.dp))
            Text("Uploaded.", color = MaterialTheme.colorScheme.primary)
        }
        verifyResult?.let {
            Spacer(Modifier.height(4.dp))
            Text(if (it) "Verify: all points report cal=true" else "Verify: some points are NOT calibrated", color = if (it) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
        }
    }

    Spacer(Modifier.height(8.dp))
    // Order must match ySeries below -- position ties each y-list to its label/color here.
    val activeSeries = buildList<Pair<String, Color>> {
        if (captures[CalStep.OPEN_PORT1]?.isNotEmpty() == true) add("Open1 (S11)" to ChartColors.CalOpen1)
        if (captures[CalStep.SHORT_PORT1]?.isNotEmpty() == true) add("Short1 (S11)" to ChartColors.CalShort1)
        if (captures[CalStep.OPEN_PORT2]?.isNotEmpty() == true) add("Open2 (S22)" to ChartColors.CalOpen2)
        if (captures[CalStep.SHORT_PORT2]?.isNotEmpty() == true) add("Short2 (S22)" to ChartColors.CalShort2)
        if (captures[CalStep.THRU]?.isNotEmpty() == true) {
            add("Thru (S21)" to ChartColors.CalThruS21)
            add("Thru (S12)" to ChartColors.CalThruS12)
        }
    }
    val ySeries = buildList<List<Double?>> {
        captures[CalStep.OPEN_PORT1]?.takeIf { it.isNotEmpty() }?.let { add(it.map { p -> p.s11_db }) }
        captures[CalStep.SHORT_PORT1]?.takeIf { it.isNotEmpty() }?.let { add(it.map { p -> p.s11_db }) }
        captures[CalStep.OPEN_PORT2]?.takeIf { it.isNotEmpty() }?.let { add(it.map { p -> p.s22_db }) }
        captures[CalStep.SHORT_PORT2]?.takeIf { it.isNotEmpty() }?.let { add(it.map { p -> p.s22_db }) }
        captures[CalStep.THRU]?.takeIf { it.isNotEmpty() }?.let { thru ->
            add(thru.map { p -> p.s21_db })
            add(thru.map { p -> p.s12_db })
        }
    }
    // All captured steps share the one locked grid in normal use, so any non-empty capture's
    // frequencies are representative for axis labels (see SnaChart's doc comment for the rare
    // different-length-series edge case this doesn't perfectly handle).
    val freqs = CalStep.entries.firstNotNullOfOrNull { captures[it]?.takeIf { p -> p.isNotEmpty() } }
    Panel(title = "Captured curves preview") {
        SnaChart(
            series = activeSeries,
            ySeries = ySeries,
            xLabelFormatter = { idx -> freqs?.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" },
            modifier = Modifier.fillMaxWidth().height(300.dp),
        )
    }
}
