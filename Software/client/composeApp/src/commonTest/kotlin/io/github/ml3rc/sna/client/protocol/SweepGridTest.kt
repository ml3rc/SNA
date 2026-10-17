package io.github.ml3rc.sna.client.protocol

import io.github.ml3rc.sna.client.data.Touchstone
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class SweepGridTest {
    private val f0 = FrequencyLimits.MIN_HZ
    private val f1 = FrequencyLimits.MAX_HZ

    @Test
    fun linearGridIsTheFirmwaresExactIntegerInterpolation() {
        assertEquals(f0, SweepGrid.frequencyAt(f0, f1, 201, 0))
        assertEquals(1_745_750_000L, SweepGrid.frequencyAt(f0, f1, 201, 57))
        assertEquals(f1, SweepGrid.frequencyAt(f0, f1, 201, 200))
        assertEquals(f0, SweepGrid.frequencyAt(f0, f1, 1, 0))
        // Integer division truncates, like the firmware.
        assertEquals(50_000_000L, SweepGrid.frequencyAt(50_000_000L, 50_000_001L, 3, 1))
    }

    @Test
    fun logGridIsGeometricRoundedAndHitsBothEndsExactly() {
        assertEquals(547_722_558L, SweepGrid.frequencyAt(f0, f1, 3, 1, log = true))
        assertEquals(195_675_593L, SweepGrid.frequencyAt(f0, f1, 201, 57, log = true))
        assertEquals(f0, SweepGrid.frequencyAt(f0, f1, 201, 0, log = true))
        assertEquals(f1, SweepGrid.frequencyAt(f0, f1, 201, 200, log = true))
    }

    @Test
    fun placeholdersAreUnmeasuredGapsAndNeverExported() {
        val grid = SweepGrid.placeholders(f0, f1, 3)
        assertEquals(listOf(f0, 3_025_000_000L, f1), grid.map { it.f })
        for (p in grid) {
            assertFalse(p.measured || p.locked || p.cal)
            assertNull(p.s11_db) // a gap, not 0 dB
        }
        val measured = SweepPoint(pt = 0, f = f0, s11_db = -10.0, s21_db = -1.0, s12_db = -1.0, s22_db = -10.0)
        val partial = listOf(measured) + grid.drop(1)
        assertFalse(Touchstone.hasCompleteTwoPortData(partial))
        val s1p = Touchstone.formatS1P(partial, 50)
        assertTrue(s1p.contains("$f0 -10.0 0.0"))
        assertFalse(s1p.contains("$f1 "))
        assertTrue(measured.measured)
    }

    @Test
    fun everyCalibrationSetCombinationExists() {
        val sets = SParam.entries.flatMap { s -> Impedance.entries.map { z -> CalSet.of(s, z) } }
        assertEquals(CalSet.entries.toSet(), sets.toSet())
        for (set in CalSet.entries) assertEquals("${set.sParam.wireName}_${set.z.ohms}", set.wireName)
    }
}
