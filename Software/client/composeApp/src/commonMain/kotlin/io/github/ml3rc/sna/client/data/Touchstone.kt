package io.github.ml3rc.sna.client.data

import io.github.ml3rc.sna.client.protocol.SweepPoint

/**
 * Touchstone .s1p/.s2p export, per `Software/docs/firmware/protocol/PROTOCOL.md`'s data-export section --
 * for interop with other RF tools, notably comparing against LibreVNA reference measurements.
 *
 * This instrument measures scalar amplitude only (no phase). Touchstone's DB format still
 * requires an angle column per S-parameter, so it's written as `0.0` -- NOT a real phase
 * measurement. Flagged with a comment line in the file itself so nobody mistakes it for one.
 */
object Touchstone {
    /** Every point measured (no placeholders) and carrying all four S-parameters. */
    fun hasCompleteTwoPortData(points: List<SweepPoint>): Boolean = points.isNotEmpty() &&
        points.all { it.measured && it.s11_db != null && it.s21_db != null && it.s12_db != null && it.s22_db != null }

    fun formatS1P(points: List<SweepPoint>, referenceOhms: Int): String = buildString {
        appendLine("! SNA scalar network analyzer export")
        appendLine("! Amplitude only (no phase) -- angle column is always 0.0, not a measurement")
        appendLine("# Hz S DB R $referenceOhms")
        points.filter { it.measured }.sortedBy { it.f }.forEach { p ->
            val s11 = p.s11_db ?: return@forEach
            if (!p.locked) appendLine("! Source lock unconfirmed at ${p.f} Hz")
            appendLine("${p.f} $s11 0.0")
        }
    }

    fun formatS2P(points: List<SweepPoint>, referenceOhms: Int): String = buildString {
        require(hasCompleteTwoPortData(points)) { "S2P export requires all four measured S-parameters at every point" }
        appendLine("! SNA scalar network analyzer export")
        appendLine("! Amplitude only (no phase) -- angle columns are always 0.0, not measurements")
        appendLine("# Hz S DB R $referenceOhms")
        points.sortedBy { it.f }.forEach { p ->
            val s11 = p.s11_db!!
            val s21 = p.s21_db!!
            val s12 = p.s12_db!!
            val s22 = p.s22_db!!
            if (!p.locked) appendLine("! Source lock unconfirmed at ${p.f} Hz")
            appendLine("${p.f} $s11 0.0 $s21 0.0 $s12 0.0 $s22 0.0")
        }
    }
}
