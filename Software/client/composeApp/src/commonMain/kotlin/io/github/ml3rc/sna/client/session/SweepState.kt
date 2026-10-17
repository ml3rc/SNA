package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.CAL_CAPTURE_ATTEN
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import io.github.ml3rc.sna.client.protocol.SweepGrid
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch

/** Inputs of an S-parameter sweep. Texts stay strings so half-typed values survive. */
data class SweepSettings(
    val f0: String = FrequencyLimits.MIN_HZ.toString(),
    val f1: String = FrequencyLimits.MAX_HZ.toString(),
    val points: String = "201",
    /** Logarithmic frequency spacing (the chart's X axis follows the grid, so it reads as a log axis). */
    val log: Boolean = false,
    val z: Impedance = Impedance.FIFTY,
    val params: Set<SParam> = setOf(SParam.S11, SParam.S21),
    val loop: Boolean = false,
    /** 0.25 dB steps below maximum. Defaults to the level the calibration curves are captured at. */
    val attenSteps: Int = CAL_CAPTURE_ATTEN,
) {
    val f0Hz: Long? get() = f0.trim().toLongOrNull()
    val f1Hz: Long? get() = f1.trim().toLongOrNull()
    val pointCount: Int? get() = points.trim().toIntOrNull()

    /** Why these settings cannot be swept, or null. */
    val problem: String?
        get() {
            val start = f0Hz
            val stop = f1Hz
            return when {
                start == null || stop == null || !FrequencyLimits.isValidRange(start, stop) ->
                    "Frequency range must be within 50 MHz–6 GHz, with start ≤ stop"
                pointCount == null || pointCount!! !in ProtocolLimits.SWEEP_POINTS -> "Sweep points must be in ${ProtocolLimits.SWEEP_POINTS}"
                params.isEmpty() -> "Select at least one S-parameter"
                else -> null
            }
        }
}

/**
 * The S-parameter sweep: its settings, the measured points and the running acquisition. Owned by
 * the session, so settings, results and a running loop survive tab changes.
 *
 * Points PERSIST across sweeps with the same grid (range, count, spacing, impedance, parameters):
 * each new pass overwrites the previous one's values in place, so the chart keeps the last-known
 * trace instead of flashing blank every loop pass. [scanIndex] tracks how far the current pass got,
 * so the chart can draw not-yet-refreshed points as stale. A changed grid resets [points] to
 * placeholders spanning the new range.
 */
class SweepState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val _settings = MutableStateFlow(SweepSettings())
    val settings: StateFlow<SweepSettings> = _settings.asStateFlow()

    private val _points = MutableStateFlow<List<SweepPoint>>(emptyList())
    val points: StateFlow<List<SweepPoint>> = _points.asStateFlow()

    private val _running = MutableStateFlow(false)
    /** A sweep (or a loop of them, including the gaps between passes) is in flight. */
    val running: StateFlow<Boolean> = _running.asStateFlow()

    private val _looping = MutableStateFlow(false)
    val looping: StateFlow<Boolean> = _looping.asStateFlow()

    private val _scanIndex = MutableStateFlow<Int?>(null)
    /** Index of the newest point of the pass in flight, or null when idle. */
    val scanIndex: StateFlow<Int?> = _scanIndex.asStateFlow()

    private val _measurementZ = MutableStateFlow(50)
    /** The impedance [points] were measured at. */
    val measurementZ: StateFlow<Int> = _measurementZ.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    val reference = SweepReferenceState()

    private data class Grid(val f0: Long, val f1: Long, val count: Int, val log: Boolean, val z: Impedance, val params: Set<SParam>)
    private var lastGrid: Grid? = null
    private var job: Job? = null

    fun update(change: (SweepSettings) -> SweepSettings) = _settings.update(change)

    /** Holds the current points as the comparison reference (not while sweeping). */
    fun holdReference(): Boolean = !_running.value && reference.hold(_points.value, _measurementZ.value)

    /** Starts a sweep with the current settings; [SweepSettings.loop] repeats it until [abort]. */
    fun start() {
        val s = _settings.value
        s.problem?.let { _error.value = it; return }
        if (!_running.compareAndSet(false, true)) return
        _error.value = null
        _looping.value = s.loop
        val f0 = s.f0Hz!!
        val f1 = s.f1Hz!!
        val n = s.pointCount!!
        val grid = Grid(f0, f1, n, s.log, s.z, s.params)
        val previous = job
        job = scope.launch(start = CoroutineStart.LAZY) {
            previous?.join()
            try {
                // A loop keeps the sweep mode for all its passes.
                client.withMode(DeviceMode.SWEEP) { do {
                    if (grid != lastGrid) {
                        // z is part of the grid: points and their impedance always change together.
                        _points.value = SweepGrid.placeholders(f0, f1, n, s.log)
                        _measurementZ.value = s.z.ohms
                        lastGrid = grid
                    }
                    _scanIndex.value = null
                    val live = LivePoints(_points.value, _points) { _scanIndex.value = it }
                    try {
                        // The level is read per pass, so a loop picks up a changed slider.
                        client.sweep(f0, f1, n, s.log, s.z, s.params, _settings.value.attenSteps).collect(live::put)
                    } catch (e: Exception) {
                        if (e is CancellationException && e !is TimeoutCancellationException) throw e
                        live.flush()
                        _error.value = e.message
                        _looping.value = false // don't keep looping into repeated failures
                        // The untouched tail of an interrupted repeat belongs to the previous pass.
                        // Clear it so charts/exports cannot present it as this pass's measurements.
                        val last = _scanIndex.value ?: -1
                        _points.value = _points.value.mapIndexed { index, point ->
                            if (index <= last) point else SweepPoint.placeholder(index, point.f)
                        }
                    }
                    _scanIndex.value = null
                } while (_looping.value && currentCoroutineContext().isActive) }
            } catch (e: SnaProtocolException) {
                _error.value = e.message // the device could not enter the sweep mode
            } finally {
                if (currentCoroutineContext()[Job] === job) {
                    _running.value = false
                    _looping.value = false
                    _scanIndex.value = null
                }
            }
        }
        job?.start()
    }

    /** The output level: applies to the next sweep, and to a running one from its next point on. */
    fun setLevel(attenSteps: Int) {
        val steps = attenSteps.coerceIn(ProtocolLimits.ATTEN)
        _settings.update { it.copy(attenSteps = steps) }
        if (!_running.value) return
        scope.launch { runCatching { client.set(atten = steps) }.onFailure { _error.value = it.message } }
    }

    /** Ends loop mode and aborts the sweep in flight. */
    fun abort() {
        _looping.value = false
        if (!_running.value) return
        scope.launch { runCatching { client.abort() }.onFailure { _error.value = it.message } }
    }

    /** Ends loop mode after the pass in flight. */
    fun stopLoop() { _looping.value = false }

    /** The connection is going away: stop without talking to the device. */
    fun cancelLocal() {
        job?.cancel()
        _running.value = false
        _looping.value = false
        _scanIndex.value = null
    }
}
