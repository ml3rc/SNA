package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SweepGrid
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.flow.MutableStateFlow
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class LivePointsTest {
    @Test
    fun publishesThrottledButNeverLosesThePointsOrTheLastOne() {
        val grid = SweepGrid.placeholders(100_000_000, 200_000_000, 1000)
        val target = MutableStateFlow(grid)
        var publishes = 0
        var newest = -1
        // An interval no test run reaches: only the last point (and flush) publish.
        val live = LivePoints(grid, target, intervalMs = 60_000) { publishes++; newest = it }
        repeat(999) { i -> live.put(SweepPoint(pt = i, f = grid[i].f, s11_db = -i.toDouble())) }
        assertTrue(publishes <= 1) // the first put may publish (nothing published yet)
        live.flush()
        assertEquals(998, newest)
        assertTrue(target.value.take(999).all { it.measured } && !target.value.last().measured)
        live.put(SweepPoint(pt = 999, f = grid[999].f, s11_db = -999.0))
        assertEquals(999, newest)
        assertEquals(-999.0, target.value.last().s11_db)
        assertFailsWith<IllegalArgumentException> { live.put(SweepPoint(pt = 1000, f = 0)) }
    }
}
