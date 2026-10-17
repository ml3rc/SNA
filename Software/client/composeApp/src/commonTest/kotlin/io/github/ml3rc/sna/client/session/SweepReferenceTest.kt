package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SweepReferenceTest {
    @Test fun holdsAnIndependentSnapshotAndPreservesItOnInvalidCapture() {
        val state = SweepReferenceState()
        val original = SweepPoint(0, 50_000_000, s21_db = -3.0)
        val points = mutableListOf(original)
        assertTrue(state.hold(points, 75))
        points[0] = original.copy(s21_db = -9.0)
        assertEquals(original, state.reference.value!!.points.single())
        assertEquals(75, state.reference.value!!.impedance)
        assertFalse(state.hold(emptyList(), 50))
        assertFalse(state.hold(listOf(original.copy(locked = false)), 50))
        assertFalse(state.hold(listOf(original.copy(s21_db = Double.NaN)), 50))
        assertEquals(original, state.reference.value!!.points.single())
        state.clear()
        assertNull(state.reference.value)
    }

    @Test fun requiresEveryFrequencyAndImpedanceToMatch() {
        val points = listOf(SweepPoint(0, 50_000_000), SweepPoint(1, 100_000_000), SweepPoint(2, 200_000_000))
        val reference = SweepReference(50, points)
        assertTrue(reference.matches(points, 50))
        assertFalse(reference.matches(points, 75))
        assertFalse(reference.matches(points.dropLast(1), 50))
        val differentMiddle = points.toMutableList().also { it[1] = it[1].copy(f = 90_000_000) }
        assertFalse(reference.matches(differentMiddle, 50))
        assertNull(reference.difference(differentMiddle, 50))
        assertNull(reference.difference(points, 75))
    }

    @Test fun subtractsDbForAllParametersAndLeavesInputUnchanged() {
        val held = SweepPoint(0, 6_000_000_000, -12.0, -3.0, -4.0, -20.0, cal = false)
        val live = held.copy(s11_db = -10.0, s21_db = -6.0, s12_db = -5.5, s22_db = -21.0)
        val delta = SweepReference(50, listOf(held)).difference(listOf(live), 50)!!.single()
        assertEquals(2.0, delta.s11_db)
        assertEquals(-3.0, delta.s21_db)
        assertEquals(-1.5, delta.s12_db)
        assertEquals(-1.0, delta.s22_db)
        assertEquals(held.f, delta.f)
        assertFalse(delta.cal)
        assertEquals(-6.0, live.s21_db)
        assertEquals(-3.0, held.s21_db)
    }

    @Test fun createsGapsForInvalidPairsAndDifferentCalibrationStatus() {
        val held = SweepPoint(0, 50_000_000, s11_db = -10.0, s21_db = -3.0)
        fun delta(point: SweepPoint, reference: SweepPoint = held) =
            SweepReference(50, listOf(reference)).difference(listOf(point), 50)!!.single()
        assertNull(delta(held.copy(locked = false)).s21_db)
        assertNull(delta(held, held.copy(locked = false)).s21_db)
        assertNull(delta(held.copy(cal = false)).s21_db)
        assertNull(delta(held.copy(s21_db = null)).s21_db)
        assertNull(delta(held.copy(s21_db = Double.POSITIVE_INFINITY)).s21_db)
        assertNull(delta(held, held.copy(s21_db = Double.NaN)).s21_db)
        assertNotNull(delta(held.copy(s21_db = null)).s11_db)
        assertNull(delta(held.copy(s21_db = Double.MAX_VALUE), held.copy(s21_db = -Double.MAX_VALUE)).s21_db)
    }
}
