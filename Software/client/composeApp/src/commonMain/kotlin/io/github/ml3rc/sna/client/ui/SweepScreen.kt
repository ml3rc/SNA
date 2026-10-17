package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.data.FileStore
import io.github.ml3rc.sna.client.data.SweepFile
import io.github.ml3rc.sna.client.data.SweepFileIO
import io.github.ml3rc.sna.client.data.Touchstone
import io.github.ml3rc.sna.client.protocol.CAL_CAPTURE_ATTEN
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint
import io.github.ml3rc.sna.client.session.MarkerSearch
import io.github.ml3rc.sna.client.session.ReferenceView
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.session.TraceMarker
import io.github.ml3rc.sna.client.session.confirmedValue
import io.github.ml3rc.sna.client.session.findTraceMarker
import kotlinx.coroutines.launch

/**
 * The Sweep tab: the S-parameter view and the raw detector view. A running raw sweep keeps its
 * view when the tab is reopened.
 */
@Composable
fun SweepScreen(session: Session, compactUi: Boolean = false) {
    var raw by remember { mutableStateOf(session.rawSweep.running.value) }
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.padding(horizontal = if (compactUi) 8.dp else 14.dp, vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ChoiceChip("S-parameters", !raw) { raw = false }
            ChoiceChip("Raw detector", raw) { raw = true }
        }
        Box(Modifier.weight(1f).fillMaxWidth()) {
            if (raw) RawSweepScreen(session, compactUi) else SParameterSweepScreen(session, compactUi)
        }
    }
}

private enum class ChartView { MERGED, SEPARATE }

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun SParameterSweepScreen(session: Session, compactUi: Boolean = false) {
    val sweep = session.sweep
    val settings by sweep.settings.collectAsState()
    val points by sweep.points.collectAsState()
    val measurementZ by sweep.measurementZ.collectAsState()
    val running by sweep.running.collectAsState()
    val looping by sweep.looping.collectAsState()
    val scanIndex by sweep.scanIndex.collectAsState()
    val error by sweep.error.collectAsState()
    val reference by sweep.reference.reference.collectAsState()
    val rawRunning by session.rawSweep.running.collectAsState()
    val toolGate = rememberGate(session, Tool.SWEEP)
    // Both views are the Sweep tool; the other view's sweep locks this one out.
    val gate = ToolGate(toolGate.reason ?: if (rawRunning) "A raw detector sweep is running." else null, running)
    val editable = gate.idleEditable

    // Display-only choices: they never reach the device, so they stay usable during a sweep.
    var chartViewName by rememberSaveable { mutableStateOf(ChartView.MERGED.name) }
    var markerSearchName by rememberSaveable { mutableStateOf(MarkerSearch.OFF.name) }
    var markerParamName by rememberSaveable { mutableStateOf(SParam.S11.name) }
    var referenceViewName by rememberSaveable { mutableStateOf(ReferenceView.OVERLAY.name) }
    val chartView = ChartView.entries.firstOrNull { it.name == chartViewName } ?: ChartView.MERGED
    val coroutineScope = rememberCoroutineScope()
    val params = settings.params
    // The traces shown are the ones measured, so toggling a parameter mid-loop doesn't hide data.
    val shownParams = SParam.entries.filter { p -> p in params || points.any { it.measured && it.value(p) != null } }.toSet()

    val markerSearch = MarkerSearch.entries.firstOrNull { it.name == markerSearchName } ?: MarkerSearch.OFF
    val referenceView = if (reference == null) ReferenceView.LIVE
        else ReferenceView.entries.firstOrNull { it.name == referenceViewName } ?: ReferenceView.OVERLAY
    val referenceMatches = reference?.matches(points, measurementZ) == true
    val displayPoints = remember(points, reference, measurementZ, referenceView) {
        if (referenceView == ReferenceView.DIFFERENCE) {
            reference?.difference(points, measurementZ) ?: points.map {
                it.copy(s11_db = null, s21_db = null, s12_db = null, s22_db = null)
            }
        } else points
    }
    val overlay = reference?.points?.takeIf { referenceMatches && referenceView == ReferenceView.OVERLAY }
    val markerParam = SParam.entries.firstOrNull { it.name == markerParamName }?.takeIf { it in shownParams } ?: shownParams.firstOrNull()
    val marker = remember(displayPoints, markerParam, markerSearch, running) {
        if (running || markerParam == null) null else findTraceMarker(displayPoints, markerParam, markerSearch)
    }

    // Keep charts bounded and readable when controls exceed the window's available height.
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val desktopResultsHeight = (maxHeight - 420.dp).coerceAtLeast(440.dp)
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(if (compactUi) 8.dp else 14.dp)) {
            Text("Frequency sweep", style = MaterialTheme.typography.headlineSmall)
            Spacer(Modifier.height(8.dp))
            Panel(title = "Acquisition", modifier = Modifier.fillMaxWidth()) {
                Hint("Supported range: 50 MHz–6 GHz")
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    NumberField(settings.f0, { v -> sweep.update { it.copy(f0 = v) } }, "Start (Hz)", Modifier.weight(1f), editable)
                    NumberField(settings.f1, { v -> sweep.update { it.copy(f1 = v) } }, "Stop (Hz)", Modifier.weight(1f), editable)
                    if (!compactUi) NumberField(settings.points, { v -> sweep.update { it.copy(points = v) } }, "Points", Modifier.weight(1f), editable)
                }
                if (compactUi) {
                    Spacer(Modifier.height(4.dp))
                    NumberField(settings.points, { v -> sweep.update { it.copy(points = v) } }, "Points", Modifier.fillMaxWidth(), editable)
                }
                Spacer(Modifier.height(6.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    SpacingChoice(settings.log, editable) { log -> sweep.update { it.copy(log = log) } }
                    ImpedanceChoice(settings.z, editable) { z -> sweep.update { it.copy(z = z) } }
                }
                Spacer(Modifier.height(6.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    SParam.entries.forEach { p ->
                        ToggleChip(p.name, p in params, { on -> sweep.update { it.copy(params = if (on) it.params + p else it.params - p) } },
                            enabled = editable, accent = p.color())
                    }
                    ToggleChip("Loop", settings.loop, { on -> sweep.update { it.copy(loop = on) } }, enabled = editable)
                }
                Spacer(Modifier.height(6.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Button(enabled = editable && settings.problem == null, onClick = { sweep.start() }) {
                        Text(if (looping) "Looping…" else if (running) "Sweeping…" else if (settings.loop) "Start loop" else "Start sweep")
                    }
                    if (looping) OutlinedButton(onClick = { sweep.stopLoop() }) { Text("Stop after pass") }
                    OutlinedButton(enabled = running, onClick = { sweep.abort() }) { Text("Abort") }
                }
                if (!running) settings.problem?.let { Hint(it) }
                ErrorText(gate.reason, "sweep-blocked")
                ErrorText(error)
                val unlockedPoints = points.count { it.measured && !it.locked }
                if (unlockedPoints > 0) ErrorText("$unlockedPoints points measured without confirmed source lock")
            }
            Spacer(Modifier.height(6.dp))
            Panel(modifier = Modifier.fillMaxWidth()) {
                // Sent with every sweep (each loop pass reads it again); during a sweep it also changes the
                // running pass from its next point on, which shows as a step in the trace.
                LevelSlider(settings.attenSteps, { v -> sweep.update { it.copy(attenSteps = v) } },
                    label = if (compactUi) null else "Relative output", enabled = gate.liveEditable,
                    onCommit = { sweep.setLevel(sweep.settings.value.attenSteps) })
                Hint("Attenuation relative to maximum output power (0.25 dB/step); during a sweep it changes the running pass " +
                    "from its next point on. Calibration captures are always taken at ${formatAtten(CAL_CAPTURE_ATTEN)}; " +
                    "measuring at another level can leave errors where the detector reading is small. " + OUTPUT_LEVEL_NOTE)
            }
            Spacer(Modifier.height(6.dp))
            Panel(title = "Reference comparison", modifier = Modifier.fillMaxWidth()) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    OutlinedButton(enabled = !running && points.any { point -> SParam.entries.any { point.confirmedValue(it) != null } }, onClick = {
                        if (sweep.holdReference()) referenceViewName = ReferenceView.OVERLAY.name
                    }) { Text(if (reference == null) "Hold reference" else "Replace reference") }
                    OutlinedButton(enabled = reference != null, onClick = { sweep.reference.clear() }) { Text("Clear reference") }
                    if (reference != null) {
                        ChoiceChip("Live", referenceView == ReferenceView.LIVE) { referenceViewName = ReferenceView.LIVE.name }
                        ChoiceChip("Overlay", referenceView == ReferenceView.OVERLAY) { referenceViewName = ReferenceView.OVERLAY.name }
                        ChoiceChip("Difference (dB)", referenceView == ReferenceView.DIFFERENCE) { referenceViewName = ReferenceView.DIFFERENCE.name }
                    }
                }
                val held = reference
                if (held != null) {
                    Text("Reference: ${held.points.size} samples · ${held.impedance} Ω · ${formatFreqAxisValue(held.points.first().f.toDouble())}–${formatFreqAxisValue(held.points.last().f.toDouble())}",
                        modifier = Modifier.testTag("reference-summary"), style = MaterialTheme.typography.bodySmall)
                    if (!referenceMatches) Text("Comparison needs the same frequency grid and impedance.",
                        modifier = Modifier.testTag("reference-mismatch"), style = MaterialTheme.typography.bodySmall)
                    else if (referenceView == ReferenceView.DIFFERENCE) Text("Current − reference in dB. Gaps indicate missing, unlocked or different calibration status.",
                        style = MaterialTheme.typography.bodySmall)
                } else Text("Hold a sweep to compare the next measurement. Use the same setup and output level.",
                    style = MaterialTheme.typography.bodySmall)
            }
            Spacer(Modifier.height(6.dp))
            val resultsModifier = if (compactUi) {
                Modifier.fillMaxWidth().height((if (chartView == ChartView.SEPARATE) shownParams.size.coerceAtLeast(1) * 240 + 100 else 360).dp +
                    if (markerSearch == MarkerSearch.OFF) 64.dp else 180.dp)
            } else Modifier.fillMaxWidth().height(desktopResultsHeight)
            Panel(title = if (referenceView == ReferenceView.DIFFERENCE) "Difference from reference (dB)" else "Magnitude response (dB)",
                modifier = resultsModifier.testTag("sweep-results")) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    ChoiceChip("Merged", chartView == ChartView.MERGED) { chartViewName = ChartView.MERGED.name }
                    ChoiceChip("Separate", chartView == ChartView.SEPARATE) { chartViewName = ChartView.SEPARATE.name }
                    val isTwoPort = shownParams.any { it != SParam.S11 }
                    OutlinedButton(enabled = !running && if (isTwoPort) Touchstone.hasCompleteTwoPortData(points)
                        else points.any { it.s11_db != null }, onClick = {
                        coroutineScope.launch {
                            val content = if (isTwoPort) Touchstone.formatS2P(points, measurementZ) else Touchstone.formatS1P(points, measurementZ)
                            FileStore.save(if (isTwoPort) "sweep.s2p" else "sweep.s1p", content)
                        }
                    }) { Text("Export Touchstone") }
                    OutlinedButton(enabled = !running && points.any { p -> SParam.entries.any { p.value(it) != null } }, onClick = {
                        coroutineScope.launch {
                            // f0/f1 describe the sweep's grid; only points that were actually measured are saved.
                            val file = SweepFile(z = measurementZ, f0 = points.first().f, f1 = points.last().f, points = points.filter { it.measured })
                            FileStore.save("sweep.json", SweepFileIO.encode(file))
                        }
                    }) { Text("Export JSON") }
                }
                if (shownParams.any { it != SParam.S11 } && !Touchstone.hasCompleteTwoPortData(points)) {
                    Text("S2P export needs a sweep with all four S-parameters. JSON supports partial measurements.", style = MaterialTheme.typography.bodySmall)
                }
                Spacer(Modifier.height(8.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    ChoiceChip("Marker off", markerSearch == MarkerSearch.OFF) { markerSearchName = MarkerSearch.OFF.name }
                    ChoiceChip("Find minimum", markerSearch == MarkerSearch.MINIMUM) { markerSearchName = MarkerSearch.MINIMUM.name }
                    ChoiceChip("Find maximum", markerSearch == MarkerSearch.MAXIMUM) { markerSearchName = MarkerSearch.MAXIMUM.name }
                }
                if (markerSearch != MarkerSearch.OFF) {
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        shownParams.forEach { parameter ->
                            ChoiceChip("Marker ${parameter.name}", markerParam == parameter) { markerParamName = parameter.name }
                        }
                    }
                    Text(when {
                        running -> "Marker search resumes when acquisition stops."
                        marker == null -> "No confirmed finite readings for the selected trace."
                        else -> "M · ${if (referenceView == ReferenceView.DIFFERENCE) "Δ " else ""}${marker.parameter.name} · ${marker.frequencyHz} Hz · %.2f dB · %s".format(
                            marker.db, if (marker.calibrated) "Calibrated" else "Uncalibrated")
                    }, modifier = Modifier.testTag("marker-readout"), style = MaterialTheme.typography.bodySmall)
                    Hint("Search uses sampled points with confirmed source lock.")
                }
                Spacer(Modifier.height(8.dp))

                if (chartView == ChartView.MERGED) {
                    MergedChart(displayPoints, shownParams, scanIndex, Modifier.fillMaxWidth().weight(1f).testTag("sweep-chart"), marker, overlay)
                } else {
                    SeparateCharts(displayPoints, shownParams, scanIndex, Modifier.fillMaxWidth().weight(1f), stacked = compactUi, marker = marker, overlay = overlay)
                }
            }
        }
    }
}

private fun SweepPoint.value(p: SParam): Double? = when (p) {
    SParam.S11 -> s11_db; SParam.S21 -> s21_db; SParam.S12 -> s12_db; SParam.S22 -> s22_db
}

internal fun SParam.color(): Color = when (this) {
    SParam.S11 -> ChartColors.S11; SParam.S21 -> ChartColors.S21; SParam.S12 -> ChartColors.S12; SParam.S22 -> ChartColors.S22
}

private fun frequencyLabels(points: List<SweepPoint>): (Int) -> String =
    { idx -> points.getOrNull(idx)?.f?.toDouble()?.let(::formatFreqAxisValue) ?: "" }

@Composable
private fun MergedChart(points: List<SweepPoint>, params: Set<SParam>, scanIndex: Int?, modifier: Modifier, marker: TraceMarker?, overlay: List<SweepPoint>?) {
    val cells = SParam.entries.filter { it in params }
    SnaChart(series = cells.map { it.name to it.color() } + if (overlay != null) cells.map { "${it.name} ref" to it.color().copy(alpha = 0.45f) } else emptyList(),
        ySeries = cells.map { p -> points.map { it.value(p) } } + if (overlay != null) cells.map { p -> overlay.map { it.confirmedValue(p) } } else emptyList(),
        xLabelFormatter = frequencyLabels(points), scanIndex = scanIndex, modifier = modifier, marker = marker, liveSeriesCount = cells.size)
}

@Composable
private fun SeparateCharts(points: List<SweepPoint>, params: Set<SParam>, scanIndex: Int?, modifier: Modifier, stacked: Boolean, marker: TraceMarker?, overlay: List<SweepPoint>?) {
    val cells = SParam.entries.filter { it in params }
    @Composable
    fun cellChart(p: SParam, cellModifier: Modifier) {
        Panel(title = p.name, modifier = cellModifier) {
            SnaChart(series = listOf(p.name to p.color()) + if (overlay != null) listOf("${p.name} ref" to p.color().copy(alpha = 0.45f)) else emptyList(),
                ySeries = listOf(points.map { it.value(p) }) + if (overlay != null) listOf(overlay.map { it.confirmedValue(p) }) else emptyList(),
                xLabelFormatter = frequencyLabels(points), scanIndex = scanIndex, modifier = Modifier.fillMaxWidth().weight(1f),
                marker = marker?.takeIf { it.parameter == p }, liveSeriesCount = 1)
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
