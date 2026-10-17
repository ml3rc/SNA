package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint

enum class MarkerSearch { OFF, MINIMUM, MAXIMUM }

data class TraceMarker(val index: Int, val parameter: SParam, val frequencyHz: Long, val db: Double, val calibrated: Boolean)

/** Search measured samples only; equal extrema retain the first sample in sweep order. */
fun findTraceMarker(points: List<SweepPoint>, parameter: SParam, search: MarkerSearch): TraceMarker? {
    if (search == MarkerSearch.OFF) return null
    var best: TraceMarker? = null
    points.forEachIndexed { index, point ->
        val value = when (parameter) {
            SParam.S11 -> point.s11_db
            SParam.S21 -> point.s21_db
            SParam.S12 -> point.s12_db
            SParam.S22 -> point.s22_db
        }
        if (!point.locked || value == null || !value.isFinite()) return@forEachIndexed
        val previous = best
        if (previous == null || (search == MarkerSearch.MINIMUM && value < previous.db) ||
            (search == MarkerSearch.MAXIMUM && value > previous.db)) {
            best = TraceMarker(index, parameter, point.f, value, point.cal)
        }
    }
    return best
}
