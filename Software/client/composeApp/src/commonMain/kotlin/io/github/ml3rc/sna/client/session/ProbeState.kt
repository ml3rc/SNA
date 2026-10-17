package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.CAL_CAPTURE_ATTEN
import io.github.ml3rc.sna.client.protocol.ProbeResult
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Live detector readout for troubleshooting: reads all four coupler paths repeatedly while the
 * route (generator on, or terminated) stays exactly as set. Runs in the connection scope like the
 * power meter, so it keeps its indicator across tabs and is stopped by the interlock when needed.
 */
class ProbeState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val _result = MutableStateFlow<ProbeResult?>(null)
    val result: StateFlow<ProbeResult?> = _result.asStateFlow()

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    // start() runs on the UI thread, the interlock's stop() on the connection's dispatcher.
    private val lock = Any()
    private var job: Job? = null
    private var generation = 0

    private fun isCurrent(run: Int) = synchronized(lock) { run == generation }

    fun start(intervalMs: Long = 300) = synchronized(lock) {
        job?.cancel()
        val mine = ++generation
        _error.value = null
        _running.value = true
        val holder = Any() // shares the generator mode with the generator tool
        job = scope.launch {
            try {
                client.acquireMode(DeviceMode.GENERATOR, holder)
                while (true) {
                    val value = client.probeRead()
                    if (isCurrent(mine)) _result.value = value
                    delay(intervalMs)
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                if (isCurrent(mine)) _error.value =
                    if ((e as? SnaProtocolException)?.code == "busy") "The device is busy with a sweep." else e.message
            } finally {
                client.releaseMode(holder)
                if (isCurrent(mine)) _running.value = false
            }
        }
    }

    fun stop() = synchronized(lock) {
        generation++
        job?.cancel()
        job = null
        _running.value = false
    }
}

/** One detector count is about 33 uV; the AD8318 slope is -24.5 mV/dB, so about 30 counts per dB. */
internal const val COUNTS_PER_DB = 0.0245 * 4095.0 / 3.3

/**
 * Plain-language findings for a probe result. The firmware reports numbers; this points at the
 * usual causes: readings near the detector floor or ceiling, unstable readings, a calibration made
 * at a different level than the measurement, and passive-DUT impossibilities (reflection above 0 dB).
 */
fun diagnoseProbe(r: ProbeResult): List<String> {
    val hints = mutableListOf<String>()
    val low = r.readings.filter { it.range == "low" }
    if (low.isNotEmpty()) hints += "Below the detector range (about -55 dBm) on ${low.joinToString { it.name }}: " +
        "those readings are compressed toward the floor, so ratios against them come out too small. " +
        "Lower the attenuation, or calibrate at the level you measure at."
    val high = r.readings.filter { it.range == "high" }
    if (high.isNotEmpty()) hints += "Above the detector range (about 0 dBm) on ${high.joinToString { it.name }}: clipped. Raise the attenuation."
    val noisy = r.readings.filter { (it.max - it.min) / COUNTS_PER_DB > 2.0 }
    if (noisy.isNotEmpty()) hints += "Unstable reading on ${noisy.joinToString { "${it.name} (${"%.1f".format((it.max - it.min) / COUNTS_PER_DB)} dB spread)" }}: " +
        "check connectors, the cable and nearby sources."
    val d = r.derived
    if (d != null) {
        if (!d.cal) hints += "No calibration is stored for ${r.z} \u03a9 at this frequency, so the S-parameters shown are the raw ratios " +
            "(they include path-to-path offsets and are not expected to be correct)."
        val worst = maxOf(d.refl_cal_db, d.trans_cal_db)
        if (d.cal && worst > 0.5) {
            hints += "The calibrated ${if (d.refl_cal_db >= d.trans_cal_db) d.refl else d.trans} is ${"%.1f".format(worst)} dB, above 0 dB. " +
                "A passive cable or load cannot do that, so the correction is wrong at this operating point."
            if (r.atten != CAL_CAPTURE_ATTEN) hints += "Calibration captures use ${"%.0f".format(CAL_CAPTURE_ATTEN * 0.25)} dB attenuation, " +
                "this route is at ${"%.2f".format(r.atten * 0.25)} dB. " +
                "Set the attenuation to $CAL_CAPTURE_ATTEN and watch whether the value drops below 0 dB. If it does, the detector is not linear enough across " +
                "that level difference, and measuring at the calibration level fixes it."
        }
        if (d.refl_db > 0.0 && !d.cal) hints += "Raw ${d.refl} is above 0 dB. That is normal before calibration: the backward path is coupled differently from the reference path."
    }
    return hints
}
