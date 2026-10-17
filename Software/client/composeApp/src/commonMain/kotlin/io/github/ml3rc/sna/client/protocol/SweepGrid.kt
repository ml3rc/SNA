package io.github.ml3rc.sna.client.protocol

import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.roundToLong

/**
 * The sweep's frequency grid as the firmware computes it (PROTOCOL.md `sweep`), so placeholder
 * points line up exactly with the frequencies the device will report. Linear: exact integer
 * `f0 + (f1-f0)*i/(points-1)`. Log: geometric, rounded to the nearest Hz. The first and last
 * points are always exactly `f0` and `f1`.
 */
object SweepGrid {
    fun frequencyAt(f0Hz: Long, f1Hz: Long, points: Int, i: Int, log: Boolean = false): Long = when {
        points <= 1 || i <= 0 -> f0Hz
        i >= points - 1 -> f1Hz
        log -> exp(ln(f0Hz.toDouble()) + (ln(f1Hz.toDouble()) - ln(f0Hz.toDouble())) * i / (points - 1)).roundToLong()
        else -> f0Hz + (f1Hz - f0Hz) * i / (points - 1)
    }

    /** Not-yet-measured points spanning the grid (see [SweepPoint.placeholder]). */
    fun placeholders(f0Hz: Long, f1Hz: Long, points: Int, log: Boolean = false): List<SweepPoint> =
        List(points) { i -> SweepPoint.placeholder(i, frequencyAt(f0Hz, f1Hz, points, i, log)) }
}
