package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DetectorPath
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.protocol.SweepGrid
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** Inputs of a raw detector sweep. Texts stay strings so half-typed values survive. */
data class RawSweepSettings(
    val f0: String = FrequencyLimits.MIN_HZ.toString(),
    val f1: String = FrequencyLimits.MAX_HZ.toString(),
    val points: String = "201",
    val log: Boolean = false,
    /** Source port for the whole sweep. */
    val port: Int = 1,
    val z: Impedance = Impedance.FIFTY,
    /** 0.25 dB steps below maximum; like the impedance it stays the device's setting after the sweep. */
    val attenSteps: Int = 64,
    val paths: Set<DetectorPath> = DetectorPath.entries.toSet(),
    /** Display only: ADC counts instead of detector dBm. */
    val showCounts: Boolean = false,
) {
    val f0Hz: Long? get() = f0.trim().toLongOrNull()
    val f1Hz: Long? get() = f1.trim().toLongOrNull()
    val pointCount: Int? get() = points.trim().toIntOrNull()?.takeIf { it in ProtocolLimits.SWEEP_POINTS }
    val valid: Boolean
        get() = f0Hz != null && f1Hz != null && FrequencyLimits.isValidRange(f0Hz!!, f1Hz!!) && pointCount != null && paths.isNotEmpty()
}

/**
 * The normal measurement, but the parameters are the detector's four coupler states instead of
 * S-parameters. Owns its own job and points, so it survives tab changes and shows up in the
 * interlock like any other sweep.
 */
class RawSweepState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val _settings = MutableStateFlow(RawSweepSettings())
    val settings: StateFlow<RawSweepSettings> = _settings.asStateFlow()

    private val _points = MutableStateFlow<List<SweepPoint>>(emptyList())
    val points: StateFlow<List<SweepPoint>> = _points.asStateFlow()

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** The source port and paths the points on screen were measured with. */
    private val _measured = MutableStateFlow<Pair<Int, Set<DetectorPath>>?>(null)
    val measured: StateFlow<Pair<Int, Set<DetectorPath>>?> = _measured.asStateFlow()

    private var job: Job? = null

    fun update(change: (RawSweepSettings) -> RawSweepSettings) = _settings.update(change)

    fun start() {
        val s = _settings.value
        if (!s.valid) { _error.value = "Check the frequency range, point count and selected states."; return }
        if (!_running.compareAndSet(false, true)) return
        _error.value = null
        val f0 = s.f0Hz!!
        val f1 = s.f1Hz!!
        val n = s.pointCount!!
        _points.value = SweepGrid.placeholders(f0, f1, n, s.log)
        _measured.value = s.port to s.paths
        job = scope.launch {
            val live = LivePoints(_points.value, _points)
            try {
                client.withMode(DeviceMode.SWEEP) {
                    client.sweepRaw(f0, f1, n, s.log, s.z, s.port, s.paths, s.attenSteps).collect(live::put)
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                live.flush()
                _error.value = e.message
            } finally {
                _running.value = false
            }
        }
    }

    /** The output level: applies to the next sweep, and to a running one from its next point on. */
    fun setLevel(attenSteps: Int) {
        val steps = attenSteps.coerceIn(ProtocolLimits.ATTEN)
        _settings.update { it.copy(attenSteps = steps) }
        if (!_running.value) return
        scope.launch { runCatching { client.set(atten = steps) }.onFailure { _error.value = it.message } }
    }

    /** Asks the device to stop; the running job ends when the device reports the sweep as aborted. */
    fun abort() {
        if (!_running.value) return
        scope.launch { runCatching { client.abort() }.onFailure { _error.value = it.message } }
    }

    /** The connection is going away: stop collecting without talking to the device. */
    fun cancelLocal() {
        job?.cancel()
        _running.value = false
    }

    fun toCsv(): String {
        val (_, paths) = _measured.value ?: return ""
        val used = DetectorPath.entries.filter { it in paths }
        return buildString {
            append("f_hz")
            used.forEach { append(",${it.wireName}_raw,${it.wireName}_dbm") }
            append("\n")
            _points.value.filter { it.measured }.forEach { p ->
                append(p.f)
                used.forEach { append(",${p.detectorRaw(it) ?: ""},${p.detectorDbm(it) ?: ""}") }
                append("\n")
            }
        }
    }
}
