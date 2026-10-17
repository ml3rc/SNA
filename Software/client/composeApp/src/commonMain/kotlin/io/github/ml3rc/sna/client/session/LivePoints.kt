package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.flow.MutableStateFlow

/** How often live data is republished for the UI: about 60 Hz. */
internal const val LIVE_UPDATE_MS = 16L

/**
 * Collects streamed sweep points into their grid slots and publishes the grid to [target] at most
 * every [intervalMs] (and always on the last point). Copying the list once per point made a
 * 4096-point sweep O(n²) and recomposed the chart for every single point; this keeps long loop
 * sweeps cheap. [onPublish] gets the index of the newest point in each published grid.
 */
internal class LivePoints(
    initial: List<SweepPoint>,
    private val target: MutableStateFlow<List<SweepPoint>>,
    private val intervalMs: Long = LIVE_UPDATE_MS,
    private val onPublish: (Int) -> Unit = {},
) {
    private val grid = initial.toTypedArray()
    private var newest = -1
    private var lastPublishMs = 0L
    private var dirty = false

    fun put(point: SweepPoint) {
        require(point.pt in grid.indices) { "Device returned an invalid sweep index" }
        grid[point.pt] = point
        newest = point.pt
        dirty = true
        val now = System.currentTimeMillis()
        if (point.pt == grid.lastIndex || now - lastPublishMs >= intervalMs) publish(now)
    }

    /** Publishes whatever arrived since the last publish. */
    fun flush() {
        if (dirty) publish(System.currentTimeMillis())
    }

    private fun publish(now: Long) {
        target.value = grid.toList()
        onPublish(newest)
        lastPublishMs = now
        dirty = false
    }
}
