package io.github.ml3rc.sna.client.ui

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceInfo
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SweepPoint
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.isActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.Job
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.roundToLong

/**
 * Plain coroutine-scope-backed state holder (not androidx.lifecycle.ViewModel, to keep this
 * file usable from commonMain without an extra KMP-lifecycle dependency). Its owner must call
 * [close] when finished; Android retains the owner across configuration changes.
 */
class SnaViewModel(private val transport: SerialTransport) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private val activeClient = MutableStateFlow<SnaClient?>(null)
    private var _client: SnaClient?
        get() = activeClient.value
        set(value) { activeClient.value = value }
    private var connectJob: Job? = null
    private var sweepJob: Job? = null

    /** The active connection, for screens (HK/SDI/Calibration) that issue their own direct
     *  [SnaClient] calls instead of going through this view model. Null when disconnected. */
    val client: SnaClient? get() = _client

    private val _ports = MutableStateFlow<List<SerialPortInfo>>(emptyList())
    val ports: StateFlow<List<SerialPortInfo>> = _ports.asStateFlow()

    private val _connected = MutableStateFlow(false)
    val connected: StateFlow<Boolean> = _connected.asStateFlow()

    private val _connecting = MutableStateFlow(false)
    val connecting: StateFlow<Boolean> = _connecting.asStateFlow()

    private val _deviceInfo = MutableStateFlow<DeviceInfo?>(null)
    val deviceInfo: StateFlow<DeviceInfo?> = _deviceInfo.asStateFlow()

    private val _points = MutableStateFlow<List<SweepPoint>>(emptyList())
    val points: StateFlow<List<SweepPoint>> = _points.asStateFlow()

    private val _sweeping = MutableStateFlow(false)
    val sweeping: StateFlow<Boolean> = _sweeping.asStateFlow()

    private val _scanIndex = MutableStateFlow<Int?>(null)
    /** Index of the most recently measured point in the sweep currently in flight, or null when
     *  idle -- drives the chart's "scan line" so points ahead of it (not yet refreshed this pass)
     *  can be shown as stale while points behind it are fresh. See [startSweep]. */
    val scanIndex: StateFlow<Int?> = _scanIndex.asStateFlow()

    /** (f0, f1, pointCount) of whatever is currently in [_points] -- not exposed, purely internal
     *  bookkeeping for [startSweep]'s "did the sweep range change" decision. */
    private data class Grid(val f0: Long, val f1: Long, val count: Int, val log: Boolean,
                            val z: Impedance, val params: Set<SParam>)
    private var lastGrid: Grid? = null
    private val _measurementZ = MutableStateFlow(50)
    val measurementZ = _measurementZ.asStateFlow()

    private val _looping = MutableStateFlow(false)
    /** True while in loop mode, including the brief gap between one sweep finishing and the next
     *  one starting -- UI should show "looping" (not just "sweeping") the whole time this is true. */
    val looping: StateFlow<Boolean> = _looping.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    fun refreshPorts() {
        scope.launch {
            runCatching { transport.listPorts() }
                .onSuccess { _ports.value = it }
                .onFailure { _error.value = it.message }
        }
    }

    fun connect(port: SerialPortInfo, onConnected: () -> Unit) {
        // Guard against a second click firing a second connect() while the first is still in
        // flight -- confirmed on real hardware, 2026-10-02: the UI gave no feedback while a
        // connect attempt was pending (e.g. waiting on the device's ping response), so the user
        // naturally clicked again, which raced a second transport.open() against the first and
        // failed with a confusing "Failed to open COM4" since the first attempt already held it.
        if (_connected.value || !_connecting.compareAndSet(false, true)) return
        _error.value = null

        val previous = connectJob
        connectJob = scope.launch(start = CoroutineStart.LAZY) {
            previous?.join()
            var candidate: SnaClient? = null
            var succeeded = false
            runCatching {
                transport.open(port)
                val c = SnaClient(transport, scope).also { it.start() }
                candidate = c
                // A handshake gets a shorter timeout than EEPROM/diagnostic commands.
                val info = withTimeout(3000) { c.ping() }
                currentCoroutineContext().ensureActive()
                _client = c
                _deviceInfo.value = info
                _connected.value = true
                succeeded = true
                scope.launch {
                    val failure = c.failure.filterNotNull().first()
                    if (_client === c) {
                        _error.value = failure.message
                        disconnect()
                    }
                }
            }.onSuccess {
                onConnected()
            }.onFailure {
                // Release the port on ANY failure, including a timed-out handshake after a
                // successful open -- otherwise it stays held by this failed attempt and every
                // retry keeps failing with "Failed to open COM4" until the whole app restarts.
                if (it !is CancellationException || it is TimeoutCancellationException)
                    _error.value = if (it is TimeoutCancellationException) "Device did not respond (timed out)" else it.message
            }
            if (!succeeded) {
                candidate?.stop()
                transport.close()
            }
            if (currentCoroutineContext()[Job] === connectJob) _connecting.value = false
        }
        connectJob?.start()
    }

    fun disconnect() {
        connectJob?.cancel()
        sweepJob?.cancel()
        _connecting.value = false
        _sweeping.value = false
        _scanIndex.value = null
        _looping.value = false
        val old = _client
        _client = null
        old?.stop()
        transport.close()
        _connected.value = false
        _deviceInfo.value = null
        lastGrid = null
    }

    /** Release the session when its owner finishes, not when a screen rotates. */
    fun close() {
        scope.cancel()
        disconnect()
    }

    /** [loop] = true repeats the sweep back-to-back (next one starts the instant the previous
     *  finishes) until [abortSweep] is called. No delay between iterations for now -- see
     *  project plan, this can grow a configurable delay later if it turns out to be needed.
     *
     *  Points PERSIST across sweeps with the same (f0, f1, points) grid, rather than clearing to
     *  empty each time -- each new pass overwrites the previous one's values in place (by index),
     *  so the chart keeps showing the last-known trace instead of flashing blank and rebuilding
     *  from nothing every loop iteration. [scanIndex] tracks how far the CURRENT pass has
     *  progressed, so the chart can show stale (not-yet-refreshed) vs. fresh points differently.
     *  If the grid changes (different frequency range or point count), the old data is no longer
     *  meaningful at all -- `_points` is reset to a full-width array of placeholders spanning the
     *  NEW range (correct frequencies, no S-param data yet) so the chart's axis immediately shows
     *  the new sweep's width instead of growing one point at a time from nothing. */
    fun startSweep(f0Hz: Long, f1Hz: Long, points: Int, log: Boolean, z: Impedance, params: Set<SParam>, loop: Boolean = false) {
        val c = client ?: return
        val validation = runCatching {
            FrequencyLimits.requireRange(f0Hz, f1Hz)
            require(points in 1..4096) { "Sweep points must be in 1..4096" }
            require(params.isNotEmpty()) { "Select at least one S-parameter" }
        }.exceptionOrNull()
        if (validation != null) { _error.value = validation.message; return }
        if (!_sweeping.compareAndSet(false, true)) return
        _error.value = null
        _looping.value = loop
        val grid = Grid(f0Hz, f1Hz, points, log, z, params.toSet())
        val previous = sweepJob
        sweepJob = scope.launch(start = CoroutineStart.LAZY) {
            previous?.join()
            try {
                _measurementZ.value = z.ohms
                do {
                    _sweeping.value = true
                    if (grid != lastGrid) {
                        _points.value = List(points) { i -> SweepPoint(pt = i, f = freqAt(f0Hz, f1Hz, points, i, log)) }
                        lastGrid = grid
                    }
                    _scanIndex.value = null
                    runCatching {
                        c.sweep(f0Hz, f1Hz, points, log, z, params).collect { pt ->
                            val current = _points.value
                            require(pt.pt in current.indices) { "Device returned an invalid sweep index" }
                            _points.value = current.toMutableList().also { it[pt.pt] = pt }
                            _scanIndex.value = pt.pt
                        }
                    }.onFailure {
                        if (it is CancellationException && it !is TimeoutCancellationException) throw it
                        _error.value = it.message
                        _looping.value = false // don't keep looping into repeated failures
                        // The untouched tail of an interrupted repeat belongs to the previous pass.
                        // Clear it so charts/exports cannot present it as this pass's measurements.
                        val last = _scanIndex.value ?: -1
                        _points.value = _points.value.mapIndexed { index, point ->
                            if (index <= last) point else SweepPoint(pt = index, f = point.f)
                        }
                    }
                    _scanIndex.value = null // a completed/aborted pass has no "ahead of the scan" points left
                } while (_looping.value && isActive)
            } finally {
                if (currentCoroutineContext()[Job] === sweepJob) {
                    _sweeping.value = false
                    _looping.value = false
                    _scanIndex.value = null
                }
            }
        }
        sweepJob?.start()
    }

    private fun freqAt(f0Hz: Long, f1Hz: Long, points: Int, i: Int, log: Boolean): Long =
        if (points <= 1) f0Hz else if (log)
            exp(ln(f0Hz.toDouble()) + (ln(f1Hz.toDouble()) - ln(f0Hz.toDouble())) * i / (points - 1)).roundToLong()
        else f0Hz + (f1Hz - f0Hz) * i / (points - 1)

    /** Stops loop mode (if active) and aborts whatever sweep is currently in flight on-device --
     *  one button does both jobs on the Sweep screen. */
    fun abortSweep() {
        _looping.value = false
        val c = client ?: return
        scope.launch { runCatching { c.abort() }.onFailure { _error.value = it.message } }
    }
}
