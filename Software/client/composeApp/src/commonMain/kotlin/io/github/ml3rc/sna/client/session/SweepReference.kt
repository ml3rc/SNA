package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class ReferenceView { LIVE, OVERLAY, DIFFERENCE }

data class SweepReference(val impedance: Int, val points: List<SweepPoint>) {
    fun matches(current: List<SweepPoint>, z: Int): Boolean =
        impedance == z && points.size == current.size && points.indices.all { points[it].f == current[it].f }

    /** Difference of displayed magnitudes in dB; this is not complex/vector subtraction. */
    fun difference(current: List<SweepPoint>, z: Int): List<SweepPoint>? {
        if (!matches(current, z)) return null
        return current.mapIndexed { index, point ->
            val reference = points[index]
            fun delta(parameter: SParam): Double? {
                if (point.cal != reference.cal) return null
                val live = point.confirmedValue(parameter) ?: return null
                val held = reference.confirmedValue(parameter) ?: return null
                return (live - held).takeIf { it.isFinite() }
            }
            point.copy(s11_db = delta(SParam.S11), s21_db = delta(SParam.S21),
                s12_db = delta(SParam.S12), s22_db = delta(SParam.S22))
        }
    }
}

fun SweepPoint.confirmedValue(parameter: SParam): Double? {
    if (!locked) return null
    return when (parameter) {
        SParam.S11 -> s11_db
        SParam.S21 -> s21_db
        SParam.S12 -> s12_db
        SParam.S22 -> s22_db
    }?.takeIf { it.isFinite() }
}

/** Owned by the session, so tab changes and Android activity recreation retain the snapshot. */
class SweepReferenceState {
    private val held = MutableStateFlow<SweepReference?>(null)
    val reference = held.asStateFlow()

    fun hold(points: List<SweepPoint>, z: Int): Boolean {
        if (points.none { point -> SParam.entries.any { point.confirmedValue(it) != null } }) return false
        held.value = SweepReference(z, points.toList())
        return true
    }

    fun clear() { held.value = null }
}
