package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull

class TraceMarkerTest {
    @Test fun findsExtremaForEveryTraceWithOriginalIndexAndFrequency() {
        val points = listOf(
            SweepPoint(0, 50_000_000, -10.0, -1.0, -4.0, -9.0),
            SweepPoint(1, 6_000_000_000, -20.0, -3.0, -2.0, -5.0, cal = false),
        )
        val minimumIndices = listOf(1, 1, 0, 0)
        SParam.entries.forEachIndexed { index, parameter ->
            val minimum = findTraceMarker(points, parameter, MarkerSearch.MINIMUM)!!
            val maximum = findTraceMarker(points, parameter, MarkerSearch.MAXIMUM)!!
            assertEquals(minimumIndices[index], minimum.index)
            assertEquals(1 - minimum.index, maximum.index)
            assertEquals(points[minimum.index].f, minimum.frequencyHz)
            assertEquals(parameter, minimum.parameter)
        }
        assertFalse(findTraceMarker(points, SParam.S11, MarkerSearch.MINIMUM)!!.calibrated)
    }

    @Test fun excludesPlaceholdersUnlockedAndNonfiniteReadings() {
        val points = listOf(
            SweepPoint(0, 50_000_000),
            SweepPoint(1, 60_000_000, s21_db = -100.0, locked = false),
            SweepPoint(2, 70_000_000, s21_db = Double.NaN),
            SweepPoint(3, 80_000_000, s21_db = Double.POSITIVE_INFINITY),
            SweepPoint(4, 90_000_000, s21_db = Double.NEGATIVE_INFINITY),
            SweepPoint(5, 100_000_000, s21_db = -3.0),
        )
        assertEquals(5, findTraceMarker(points, SParam.S21, MarkerSearch.MINIMUM)!!.index)
        assertEquals(-3.0, findTraceMarker(points, SParam.S21, MarkerSearch.MAXIMUM)!!.db)
        assertNull(findTraceMarker(points.dropLast(1), SParam.S21, MarkerSearch.MINIMUM))
        assertNull(findTraceMarker(points, SParam.S11, MarkerSearch.MINIMUM))
    }

    @Test fun handlesDisabledEmptySinglePointAndTiedExtrema() {
        val point = SweepPoint(0, 50_000_000, s11_db = -12.0)
        assertNull(findTraceMarker(emptyList(), SParam.S11, MarkerSearch.MINIMUM))
        assertNull(findTraceMarker(listOf(point), SParam.S11, MarkerSearch.OFF))
        assertEquals(0, findTraceMarker(listOf(point), SParam.S11, MarkerSearch.MAXIMUM)!!.index)
        val tied = listOf(point, point.copy(pt = 1, f = 60_000_000))
        assertEquals(0, findTraceMarker(tied, SParam.S11, MarkerSearch.MINIMUM)!!.index)
        assertEquals(0, findTraceMarker(tied, SParam.S11, MarkerSearch.MAXIMUM)!!.index)
    }
}
