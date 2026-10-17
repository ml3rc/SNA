package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.ml3rc.sna.client.session.TraceMarker
import kotlin.math.roundToInt

/**
 * A small line chart on a `Canvas`, with mouse/touch hover values. (It replaced the Vico library,
 * whose axis-step computation ran out of memory on jittery real frequencies and which crashed
 * when a chart's model producer changed.)
 *
 * [series] is label+color per trace, parallel to [ySeries] (one Double list per entry). X is
 * always the point INDEX (0 until the series' own size), not a data value -- so a log sweep
 * reads as a log frequency axis, there is no axis "step size" to compute, and [xLabelFormatter] maps an
 * index back to a real label (e.g. a frequency) purely for display. If series have different
 * lengths (e.g. CalibrationScreen's per-step captures, which may rarely be measured against
 * different grids), each is independently stretched to fill the full plot width by its own
 * fractional position (`i / (size - 1)`) -- axis tick labels are based on the LONGEST series, so
 * a shorter series' points won't line up exactly under the "correct" tick in that edge case. This
 * is an accepted v1 simplification: every normal use (all of Sweep, and Calibration once its grid
 * is locked) has equal-length series, where this never matters.
 *
 * [scanIndex], when non-null, marks how far a live, in-progress sweep has refreshed this pass --
 * points at or before it are drawn at full opacity ("fresh", just (re)measured), points after it
 * are drawn faded ("stale", still showing the previous pass' value until this one reaches them),
 * with a bright vertical line at the boundary itself. Pass null when idle/not sweeping, or when
 * there's nothing stale to distinguish (e.g. a one-shot calibration capture) -- the whole trace
 * then just draws at full opacity with no scan line, same as before this parameter existed.
 *
 * A null in [ySeries] means "not measured (yet)" -- e.g. a freshly-resized sweep's not-yet-reached
 * tail -- and is drawn as a GAP (no line through it), and excluded from the Y-axis autoscale.
 * Plotting it as 0 instead would be actively misleading: a wide not-yet-scanned region sitting at
 * 0 dB would drag the whole axis toward it and visually crush the real data into a sliver.
 *
 * [liveSeriesCount]: the first [liveSeriesCount] entries of [series]/[ySeries] are the live traces;
 * every entry after them is a reference (held) trace, drawn dashed and without scan-line fading. Callers must
 * therefore append reference series AFTER all live ones, in the same order on both lists.
 */
@Composable
@OptIn(ExperimentalLayoutApi::class)
fun SnaChart(
    series: List<Pair<String, Color>>,
    ySeries: List<List<Double?>>,
    xLabelFormatter: (Int) -> String,
    modifier: Modifier = Modifier,
    yLabelFormatter: (Double) -> String = { "%.1f".format(it) },
    scanIndex: Int? = null,
    marker: TraceMarker? = null,
    liveSeriesCount: Int = series.size,
) {
    val textColor = MaterialTheme.colorScheme.onSurface
    val mutedColor = MaterialTheme.colorScheme.onSurfaceVariant
    val gridColor = MaterialTheme.colorScheme.outline.copy(alpha = 0.35f)
    val tooltipBg = MaterialTheme.colorScheme.surfaceVariant
    val scanLineColor = MaterialTheme.colorScheme.primary
    val textMeasurer = rememberTextMeasurer()

    var hoverIndex by remember { mutableStateOf<Int?>(null) }

    val pointCount = ySeries.maxOfOrNull { it.size } ?: 0
    val allY = remember(ySeries) { ySeries.flatten().filterNotNull() }
    val yMin = allY.minOrNull() ?: 0.0
    val yMax = allY.maxOrNull() ?: 1.0
    val yPad = ((yMax - yMin).takeIf { it > 1e-9 } ?: 1.0) * 0.1
    val yLo = yMin - yPad
    val yHi = yMax + yPad

    Column(modifier) {
        if (series.isNotEmpty()) {
            FlowRow(
                Modifier.fillMaxWidth().padding(bottom = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                series.forEach { (label, color) ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(10.dp).background(color))
                        Text(
                            " $label",
                            style = MaterialTheme.typography.bodySmall,
                            color = textColor,
                        )
                    }
                }
            }
        }

        if (pointCount == 0) {
            Box(Modifier.fillMaxWidth().weight(1f).heightIn(min = 160.dp), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Text("Ready to measure", style = MaterialTheme.typography.titleMedium, color = textColor)
                    Text("Run a sweep to display frequency response.", style = MaterialTheme.typography.bodySmall,
                        color = mutedColor, textAlign = TextAlign.Center)
                }
            }
        } else Canvas(
            Modifier
                .fillMaxWidth()
                .weight(1f)
                .heightIn(min = 160.dp)
                .pointerInput(pointCount) {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            when (event.type) {
                                PointerEventType.Move, PointerEventType.Enter, PointerEventType.Press -> {
                                    if (pointCount > 1) {
                                        val x = event.changes.first().position.x
                                        val leftMargin = 56.dp.toPx()
                                        val rightPad = 8.dp.toPx()
                                        val plotWidth = (size.width - leftMargin - rightPad).coerceAtLeast(1f)
                                        val frac = ((x - leftMargin) / plotWidth).coerceIn(0f, 1f)
                                        hoverIndex = (frac * (pointCount - 1)).roundToInt()
                                    }
                                }
                                PointerEventType.Exit -> hoverIndex = null
                            }
                        }
                    }
                },
        ) {
            drawChart(
                series = series,
                ySeries = ySeries,
                pointCount = pointCount,
                yLo = yLo,
                yHi = yHi,
                hoverIndex = hoverIndex,
                scanIndex = scanIndex,
                marker = marker,
                liveSeriesCount = liveSeriesCount,
                textColor = textColor,
                mutedColor = mutedColor,
                gridColor = gridColor,
                tooltipBg = tooltipBg,
                scanLineColor = scanLineColor,
                textMeasurer = textMeasurer,
                xLabelFormatter = xLabelFormatter,
                yLabelFormatter = yLabelFormatter,
            )
        }
    }
}

private fun DrawScope.drawChart(
    series: List<Pair<String, Color>>,
    ySeries: List<List<Double?>>,
    pointCount: Int,
    yLo: Double,
    yHi: Double,
    hoverIndex: Int?,
    scanIndex: Int?,
    marker: TraceMarker?,
    liveSeriesCount: Int,
    textColor: Color,
    mutedColor: Color,
    gridColor: Color,
    tooltipBg: Color,
    scanLineColor: Color,
    textMeasurer: androidx.compose.ui.text.TextMeasurer,
    xLabelFormatter: (Int) -> String,
    yLabelFormatter: (Double) -> String,
) {
    val leftMargin = 56.dp.toPx()
    val bottomMargin = 24.dp.toPx()
    val topPad = 8.dp.toPx()
    val rightPad = 8.dp.toPx()
    val plotLeft = leftMargin
    val plotTop = topPad
    val plotWidth = (size.width - leftMargin - rightPad).coerceAtLeast(1f)
    val plotHeight = (size.height - topPad - bottomMargin).coerceAtLeast(1f)
    val yRange = (yHi - yLo).takeIf { it > 1e-9 } ?: 1.0

    fun yToPx(y: Double): Float = (plotTop + plotHeight * (1f - ((y - yLo) / yRange)).toFloat())
    fun xToPxFor(i: Int, n: Int): Float =
        plotLeft + plotWidth * if (n <= 1) 0f else i.toFloat() / (n - 1).toFloat()

    val smallStyle = TextStyle(color = mutedColor, fontSize = 12.sp)

    // Y gridlines + labels (5 evenly spaced steps across the padded range).
    val ySteps = 4
    for (s in 0..ySteps) {
        val value = yLo + yRange * (1.0 - s.toDouble() / ySteps)
        val py = yToPx(value)
        drawLine(gridColor, androidx.compose.ui.geometry.Offset(plotLeft, py), androidx.compose.ui.geometry.Offset(plotLeft + plotWidth, py), strokeWidth = 1f)
        val label = textMeasurer.measure(yLabelFormatter(value), smallStyle)
        drawText(label, topLeft = androidx.compose.ui.geometry.Offset(4.dp.toPx(), py - label.size.height / 2f))
    }

    // X gridlines + labels (based on the longest series).
    if (pointCount > 0) {
        // Keep frequency labels readable on phones as well as projected desktop views.
        val candidates = minOf(7, pointCount)
        val widestLabel = (0 until candidates).maxOf { tick ->
            val index = if (candidates <= 1) 0 else ((pointCount - 1) * tick.toFloat() / (candidates - 1)).roundToInt()
            textMeasurer.measure(xLabelFormatter(index), smallStyle).size.width
        }
        val xTicks = minOf(candidates, (plotWidth / (widestLabel + 12.dp.toPx())).toInt().coerceAtLeast(1) + 1)
        for (t in 0 until xTicks) {
            val idx = if (xTicks <= 1) 0 else ((pointCount - 1) * t.toFloat() / (xTicks - 1)).roundToInt()
            val px = xToPxFor(idx, pointCount)
            drawLine(gridColor, androidx.compose.ui.geometry.Offset(px, plotTop), androidx.compose.ui.geometry.Offset(px, plotTop + plotHeight), strokeWidth = 1f)
            val label = textMeasurer.measure(xLabelFormatter(idx), smallStyle)
            val lx = (px - label.size.width / 2f).coerceIn(0f, size.width - label.size.width)
            drawText(label, topLeft = androidx.compose.ui.geometry.Offset(lx, plotTop + plotHeight + 4.dp.toPx()))
        }
    }

    // Plot border.
    drawLine(gridColor, androidx.compose.ui.geometry.Offset(plotLeft, plotTop), androidx.compose.ui.geometry.Offset(plotLeft, plotTop + plotHeight), strokeWidth = 1f)
    drawLine(gridColor, androidx.compose.ui.geometry.Offset(plotLeft, plotTop + plotHeight), androidx.compose.ui.geometry.Offset(plotLeft + plotWidth, plotTop + plotHeight), strokeWidth = 1f)

    // One polyline per series, split into a fresh (full-opacity) and stale (faded) segment at
    // the scan position when a sweep is in progress -- see SnaChart's doc comment on [scanIndex].
    // A null value (not measured yet) breaks the path into a gap rather than drawing through it.
    fun pathFor(ys: List<Double?>, range: IntRange): androidx.compose.ui.graphics.Path {
        val path = androidx.compose.ui.graphics.Path()
        var started = false
        for (i in range) {
            val y = ys[i]
            if (y == null) { started = false; continue }
            val px = xToPxFor(i, ys.size)
            val py = yToPx(y)
            if (!started) { path.moveTo(px, py); started = true } else path.lineTo(px, py)
        }
        return path
    }
    ySeries.forEachIndexed { si, ys ->
        if (ys.size < 2) return@forEachIndexed
        val color = series.getOrNull(si)?.second ?: mutedColor
        val isReference = si >= liveSeriesCount
        val stroke = androidx.compose.ui.graphics.drawscope.Stroke(width = 2.dp.toPx(),
            pathEffect = if (isReference) androidx.compose.ui.graphics.PathEffect.dashPathEffect(
                floatArrayOf(6.dp.toPx(), 4.dp.toPx())) else null)
        // Scale the shared scan index into this series' own length (see doc comment on
        // different-length series).
        val scaledScan = scanIndex?.takeIf { !isReference }?.let { s ->
            if (pointCount <= 1) null else ((ys.size - 1) * s.toFloat() / (pointCount - 1)).roundToInt()
        }?.coerceIn(0, ys.size - 1)
        if (scaledScan == null || scaledScan >= ys.size - 1) {
            drawPath(pathFor(ys, ys.indices), color = color, style = stroke)
        } else {
            drawPath(pathFor(ys, 0..scaledScan), color = color, style = stroke)
            drawPath(pathFor(ys, scaledScan until ys.size), color = color.copy(alpha = 0.3f), style = stroke)
        }
    }

    // Scan line itself -- a bright marker at the live sweep's current progress, on top of the
    // fresh/stale trace split above but under the hover crosshair/tooltip.
    if (scanIndex != null && pointCount > 1) {
        val px = xToPxFor(scanIndex.coerceIn(0, pointCount - 1), pointCount)
        drawLine(
            scanLineColor,
            androidx.compose.ui.geometry.Offset(px, plotTop),
            androidx.compose.ui.geometry.Offset(px, plotTop + plotHeight),
            strokeWidth = 2.dp.toPx(),
        )
    }

    // Search marker remains visible independently of the mouse/touch hover.
    if (marker != null && marker.index in 0 until pointCount) {
        val px = xToPxFor(marker.index, pointCount)
        val py = yToPx(marker.db)
        drawLine(scanLineColor, androidx.compose.ui.geometry.Offset(px, plotTop),
            androidx.compose.ui.geometry.Offset(px, plotTop + plotHeight), strokeWidth = 1.dp.toPx())
        drawCircle(scanLineColor, radius = 5.dp.toPx(), center = androidx.compose.ui.geometry.Offset(px, py))
        drawText(textMeasurer.measure("M", TextStyle(color = textColor, fontSize = 12.sp)),
            topLeft = androidx.compose.ui.geometry.Offset((px + 7.dp.toPx()).coerceAtMost(size.width - 16.dp.toPx()), plotTop))
    }

    // Hover crosshair + tooltip.
    if (hoverIndex != null && pointCount > 0) {
        val hi = hoverIndex.coerceIn(0, pointCount - 1)
        val px = xToPxFor(hi, pointCount)
        drawLine(mutedColor, androidx.compose.ui.geometry.Offset(px, plotTop), androidx.compose.ui.geometry.Offset(px, plotTop + plotHeight), strokeWidth = 1f)

        val lines = buildList {
            add(xLabelFormatter(hi))
            series.forEachIndexed { si, (label, _) ->
                val y = ySeries.getOrNull(si)?.let { ys ->
                    // Scale the shared hover index into this series' own length (see doc comment
                    // on different-length series).
                    val scaled = if (pointCount <= 1 || ys.size <= 1) 0 else ((ys.size - 1) * hi.toFloat() / (pointCount - 1)).roundToInt()
                    ys.getOrNull(scaled)
                }
                if (y != null) add("$label: ${yLabelFormatter(y)}")
            }
        }
        val tooltipText = lines.joinToString("\n")
        val measured = textMeasurer.measure(tooltipText, TextStyle(color = textColor, fontSize = 11.sp, textAlign = TextAlign.Start))
        val boxPad = 6.dp.toPx()
        var boxLeft = px + 8.dp.toPx()
        if (boxLeft + measured.size.width + boxPad * 2 > size.width) boxLeft = px - measured.size.width - boxPad * 2 - 8.dp.toPx()
        boxLeft = boxLeft.coerceAtLeast(0f)
        val boxTop = plotTop + 4.dp.toPx()
        drawRect(
            color = tooltipBg,
            topLeft = androidx.compose.ui.geometry.Offset(boxLeft, boxTop),
            size = androidx.compose.ui.geometry.Size(measured.size.width + boxPad * 2, measured.size.height + boxPad * 2),
        )
        drawText(measured, topLeft = androidx.compose.ui.geometry.Offset(boxLeft + boxPad, boxTop + boxPad))
    }
}
