package io.github.ml3rc.sna.client.client

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

enum class TrafficDirection { TX, RX }

/** What a line is, so the console can hide the background noise. */
enum class TrafficKind {
    /** A command of this client's tools and its reply. */
    COMMAND,
    /** Background polling (`status`, `src_temp_read`) and its reply. */
    POLL,
    /** A streamed sweep point (not the sweep's final reply). */
    SWEEP_POINT,
    /** An unsolicited `event` line. */
    EVENT,
    /** A line typed into the console, or a reply to one. */
    CONSOLE,
    /** A line that is not a JSON object. */
    UNPARSED,
}

data class TrafficEntry(val seq: Long, val timeMs: Long, val direction: TrafficDirection, val kind: TrafficKind, val text: String)

/**
 * Every line written to and read from the device, newest last. Bounded to [capacity] entries, so a
 * long session (loop sweeps stream hundreds of lines per second) never grows it. Writers call
 * [add] from any thread; readers poll [revision] and take a [snapshot] when it changed.
 */
class TrafficLog(private val capacity: Int = 3000) {
    private val lock = Any()
    private val entries = ArrayDeque<TrafficEntry>(capacity)
    private var seq = 0L
    private val _revision = MutableStateFlow(0L)
    /** Changes whenever the log does. */
    val revision: StateFlow<Long> = _revision.asStateFlow()

    fun add(direction: TrafficDirection, kind: TrafficKind, text: String) {
        synchronized(lock) {
            if (entries.size == capacity) entries.removeFirst()
            entries.addLast(TrafficEntry(++seq, System.currentTimeMillis(), direction, kind, text))
        }
        _revision.update { it + 1 }
    }

    fun snapshot(): List<TrafficEntry> = synchronized(lock) { entries.toList() }

    fun clear() {
        synchronized(lock) { entries.clear() }
        _revision.update { it + 1 }
    }
}
