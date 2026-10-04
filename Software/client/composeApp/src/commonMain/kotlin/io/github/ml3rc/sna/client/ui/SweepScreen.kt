package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.data.FileStore
import io.github.ml3rc.sna.client.data.SweepFile
import io.github.ml3rc.sna.client.data.SweepFileIO
import io.github.ml3rc.sna.client.data.Touchstone
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.launch

private enum class ChartView { MERGED, SEPARATE }

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SweepScreen(viewModel: SnaViewModel, compactUi: Boolean = false) {
    val points by viewModel.points.collectAsState()
    val measurementZ by viewModel.measurementZ.collectAsState()
    val sweeping by viewModel.sweeping.collectAsState()
    val looping by viewModel.looping.collectAsState()
    val scanIndex by viewModel.scanIndex.collectAsState()
    val error by viewModel.error.collectAsState()

    var f0 by rememberSaveable { mutableStateOf(FrequencyLimits.MIN_HZ.toString()) }
    var f1 by rememberSaveable { mutableStateOf(FrequencyLimits.MAX_HZ.toString()) }
    var numPoints by rememberSaveable { mutableStateOf("201") }
    var z50 by rememberSaveable { mutableStateOf(true) }
    var showS11 by rememberSaveable { mutableStateOf(true) }
    var showS21 by rememberSaveable { mutableStateOf(true) }
    var showS12 by rememberSaveable { mutableStateOf(false) }
    var showS22 by rememberSaveable { mutableStateOf(false) }
    var loop by rememberSaveable { mutableStateOf(false) }
    var chartViewIndex by rememberSaveable { mutableStateOf(ChartView.MERGED.ordinal) }
    val chartView = ChartView.entries[chartViewIndex]
    // HMC1119 attenuation is relative to maximum output, not calibrated absolute dBm.
    var attenSteps by rememberSaveable { mutableStateOf(64f) }
    val coroutineScope = rememberCoroutineScope()
    val scroll = rememberScrollState()
    val params = buildSet {
        if (showS11) add(SParam.S11)
        if (showS21) add(SParam.S21)
        if (showS12) add(SParam.S12)
        if (showS22) add(SParam.S22)
    }

    val validFrequencyRange = f0.toLongOrNull()?.let { start ->
        f1.toLongOrNull()?.let { stop -> FrequencyLimits.isValidRange(start, stop) }
    } == true

    // A scrolling page needs bounded chart heights instead of weights in unbounded space.
    val contentModifier = if (compactUi) Modifier.verticalScroll(scroll) else Modifier
    Column(Modifier.fillMaxSize().then(contentModifier).padding(if (compactUi) 8.dp else 14.dp)) {
        Text("Frequency sweep", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))
        Panel(title = "Acquisition", modifier = Modifier.fillMaxWidth()) {
            Text("Supported range: 50 MHz–6 GHz", color = if (validFrequencyRange)
                MaterialTheme.colorScheme.onSurfaceVariant else MaterialTheme.colorScheme.error)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedTextField(value = f0, onValueChange = { f0 = it }, label = { Text("Start (Hz)") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.weight(1f))
                OutlinedTextField(value = f1, onValueChange = { f1 = it }, label = { Text("Stop (Hz)") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.weight(1f))
                if (!compactUi) {
                    OutlinedTextField(value = numPoints, onValueChange = { numPoints = it }, label = { Text("Points") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.weight(1f))
                }
            }
            if (compactUi) {
                Spacer(Modifier.height(4.dp))
                OutlinedTextField(value = numPoints, onValueChange = { numPoints = it }, label = { Text("Points") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), modifier = Modifier.fillMaxWidth())
            }
            Spacer(Modifier.height(6.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("75Ω"); Switch(checked = z50, onCheckedChange = { z50 = it }); Text("50Ω")
                }
                ToggleChip("S11", showS11, { showS11 = it }, accent = ChartColors.S11)
                ToggleChip("S21", showS21, { showS21 = it }, accent = ChartColors.S21)
                ToggleChip("S12", showS12, { showS12 = it }, accent = ChartColors.S12)
                ToggleChip("S22", showS22, { showS22 = it }, accent = ChartColors.S22)
                ToggleChip("Loop", loop, { loop = it }, enabled = !sweeping)
            }
            Spacer(Modifier.height(6.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Button(enabled = !sweeping && params.isNotEmpty() && validFrequencyRange, onClick = {
                    val f0Hz = f0.toLongOrNull() ?: return@Button
                    val f1Hz = f1.toLongOrNull() ?: return@Button
                    val n = numPoints.toIntOrNull() ?: return@Button
                    viewModel.startSweep(f0Hz, f1Hz, n, log = false, z = if (z50) Impedance.FIFTY else Impedance.SEVENTY_FIVE, params = params, loop = loop)
                }) { Text(if (looping) "Looping…" else if (sweeping) "Sweeping…" else if (loop) "Start loop" else "Start sweep") }
                OutlinedButton(enabled = sweeping || looping, onClick = { viewModel.abortSweep() }) { Text("Abort") }
            }
            error?.let { Spacer(Modifier.height(8.dp)); Text(it, color = MaterialTheme.colorScheme.error) }
            val unlockedPoints = points.count { !it.locked }
            if (unlockedPoints > 0) Text("$unlockedPoints points measured without confirmed source lock", color = MaterialTheme.colorScheme.error)
        }
        Spacer(Modifier.height(6.dp))
        Panel(modifier = Modifier.fillMaxWidth()) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                if (!compactUi) Text("Relative output", modifier = Modifier.width(112.dp), style = MaterialTheme.typography.bodyMedium)
                Text("-%.2f dB".format(attenSteps * 0.25f), modifier = Modifier.width(80.dp), style = MaterialTheme.typography.bodyLarge)
                Slider(value = attenSteps, onValueChange = { attenSteps = it }, onValueChangeFinished = {
                    val client = viewModel.client ?: return@Slider
                    coroutineScope.launch { runCatching { client.manual("atten", attenSteps.toInt()) } }
                }, valueRange = 0f..127f, steps = 126, modifier = Modifier.weight(1f),
                    colors = SliderDefaults.colors(activeTrackColor = MaterialTheme.colorScheme.primary,
                        inactiveTrackColor = MaterialTheme.colorScheme.outline,
                        activeTickColor = Color.Transparent, inactiveTickColor = Color.Transparent))
            }
            if (compactUi) Text("Attenuation relative to maximum output power (0.25 dB/step).", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.height(6.dp))
        val resultsModifier = if (compactUi) {
            Modifier.fillMaxWidth().height(if (chartView == ChartView.SEPARATE) (params.size.coerceAtLeast(1) * 240 + 100).dp else 360.dp)
        } else Modifier.fillMaxWidth().weight(1f)
        Panel(title = "Magnitude response (dB)", modifier = resultsModifier.testTag("sweep-results")) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("Merged", chartView == ChartView.MERGED) { chartViewIndex = ChartView.MERGED.ordinal }
                ChoiceChip("Separate", chartView == ChartView.SEPARATE) { chartViewIndex = ChartView.SEPARATE.ordinal }
                val z = measurementZ
                val isTwoPort = showS21 || showS12 || showS22
                OutlinedButton(enabled = !sweeping && if (isTwoPort) Touchstone.hasCompleteTwoPortData(points)
                    else points.any { it.s11_db != null }, onClick = {
                    coroutineScope.launch {
                        val content = if (isTwoPort) Touchstone.formatS2P(points, z) else Touchstone.formatS1P(points, z)
                        FileStore.save(if (isTwoPort) "sweep.s2p" else "sweep.s1p", content)
                    }
                }) { Text("Export Touchstone") }
                OutlinedButton(enabled = !sweeping && points.any { it.s11_db != null || it.s21_db != null || it.s12_db != null || it.s22_db != null }, onClick = {
                    coroutineScope.launch {
                        val file = SweepFile(z = z, f0 = points.first().f, f1 = points.last().f, points = points)
                        FileStore.save("sweep.json", SweepFileIO.encode(file))
                    }
                }) { Text("Export JSON") }
            }
            if ((showS21 || showS12 || showS22) && !Touchstone.hasCompleteTwoPortData(points)) {
                Text("S2P export needs a sweep with all four S-parameters. JSON supports partial measurements.", style = MaterialTheme.typography.bodySmall)
            }
            Spacer(Modifier.height(8.dp))

            if (chartView == ChartView.MERGED) {
                MergedChart(points, params, scanIndex, Modifier.fillMaxWidth().weight(1f))
            } else {
                SeparateCharts(points, params, scanIndex, Modifier.fillMaxWidth().weight(1f), stacked = compactUi)
            }
        }
    }
}

private data class ChartCell(val label: String, val color: Color, val y: (SweepPoint) -> Double?)

// Keep labels/colors and their data together; a single recomposition updates the entire chart.
private fun chartCells(params: Set<SParam>): List<ChartCell> = buildList {
    if (SParam.S11 in params) add(ChartCell("S11", ChartColors.S11) { it.s11_db })
    if (SParam.S21 in params) add(ChartCell("S21", ChartColors.S21) { it.s21_db })
    if (SParam.S12 in params) add(ChartCell("S12", ChartColors.S12) { it.s12_db })
    if (SParam.S22 in params) add(ChartCell("S22", ChartColors.S22) { it.s22_db })
}

@Composable
private fun MergedChart(points: List<SweepPoint>, params: Set<SParam>, scanIndex: Int?, modifier: Modifier) {
    val cells = chartCells(params)
    SnaChart(series = cells.map { it.label to it.color }, ySeries = cells.map { points.map(it.y) },
        xLabelFormatter = { idx -> points.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" },
        scanIndex = scanIndex, modifier = modifier)
}

@Composable
private fun SeparateCharts(points: List<SweepPoint>, params: Set<SParam>, scanIndex: Int?, modifier: Modifier, stacked: Boolean) {
    val cells = chartCells(params)
    @Composable
    fun cellChart(cell: ChartCell, cellModifier: Modifier) {
        Panel(title = cell.label, modifier = cellModifier) {
            SnaChart(series = listOf(cell.label to cell.color), ySeries = listOf(points.map(cell.y)),
                xLabelFormatter = { idx -> points.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" },
                scanIndex = scanIndex, modifier = Modifier.fillMaxWidth().weight(1f))
        }
    }
    if (stacked && cells.isNotEmpty()) {
        Column(modifier, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            cells.forEach { cellChart(it, Modifier.fillMaxWidth().weight(1f)) }
        }
        return
    }
    when (cells.size) {
        0 -> Panel(title = "No S-parameter selected", modifier = modifier) {}
        1 -> cellChart(cells[0], modifier)
        2 -> Row(modifier) {
            cellChart(cells[0], Modifier.weight(1f).fillMaxSize())
            Spacer(Modifier.width(8.dp))
            cellChart(cells[1], Modifier.weight(1f).fillMaxSize())
        }
        else -> Column(modifier) {
            Row(Modifier.weight(1f)) {
                cellChart(cells[0], Modifier.weight(1f).fillMaxSize())
                Spacer(Modifier.width(8.dp))
                cellChart(cells[1], Modifier.weight(1f).fillMaxSize())
            }
            Spacer(Modifier.height(8.dp))
            Row(Modifier.weight(1f)) {
                cellChart(cells[2], Modifier.weight(1f).fillMaxSize())
                Spacer(Modifier.width(8.dp))
                if (cells.size > 3) cellChart(cells[3], Modifier.weight(1f).fillMaxSize()) else Spacer(Modifier.weight(1f))
            }
        }
    }
}
