package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.ProbeResult
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/** What manual control last set. Null: not set since the mode was entered (the device keeps its own state). */
data class ManualSettings(
    val lpf: Int? = null,
    val coupler: Int? = null,
    /** 0 = terminated, 1/2 = source port. */
    val port: Int? = null,
    val sourceOn: Boolean? = null,
    val sourceFreqHz: Long? = null,
    val sourceEnabled: Boolean? = null,
)

/**
 * Bench control of single RF blocks (`manual`, mode [DeviceMode.MANUAL]). Holds the mode from
 * [enter] until [exit], so the interlock locks out every other tool meanwhile; the device itself
 * refuses `lpf`/`cplr`/`src_freq` while the source is routed to a port (`output_active`).
 */
class ManualState(private val client: SnaClient, private val scope: CoroutineScope, private val monitor: DeviceMonitor) {
    private val _active = MutableStateFlow(false)
    val active: StateFlow<Boolean> = _active.asStateFlow()

    private val _settings = MutableStateFlow(ManualSettings())
    val settings: StateFlow<ManualSettings> = _settings.asStateFlow()

    private val _probe = MutableStateFlow<ProbeResult?>(null)
    val probe: StateFlow<ProbeResult?> = _probe.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    fun enter() = command {
        client.acquireMode(DeviceMode.MANUAL, this)
        _settings.value = ManualSettings()
        _probe.value = null
        _active.value = true
    }

    /** Leaves manual control; `mode_exit` terminates the output and leaves the source running and parked. */
    fun exit() = command {
        client.releaseMode(this)
        _active.value = false
    }

    fun setLpf(value: Int) = command { client.manual("lpf", value.toLong()); _settings.update { it.copy(lpf = value) } }

    fun setCoupler(value: Int) = command { client.manual("cplr", value.toLong()); _settings.update { it.copy(coupler = value) } }

    fun setPort(value: Int) = command {
        val on = client.manual("port", value.toLong())
        _settings.update { it.copy(port = value, sourceOn = on) }
    }

    fun setSourceFrequency(hz: Long) = command {
        FrequencyLimits.requireRange(hz, hz)
        client.manual("src_freq", hz)
        _settings.update { it.copy(sourceFreqHz = hz) }
    }

    fun setSourceEnabled(on: Boolean) = command {
        client.manual("src_en", on)
        // Switching the source off parks a source that was on.
        _settings.update { it.copy(sourceEnabled = on, sourceOn = if (on) it.sourceOn else false) }
    }

    /** One detector readout of all four coupler paths with the route as it is. */
    fun readDetector() = command { _probe.value = client.probeRead() }

    /** The connection is going away (the device leaves the mode by itself). */
    fun cancelLocal() { _active.value = false }

    private fun command(block: suspend () -> Unit) {
        if (!_busy.compareAndSet(false, true)) return
        _error.value = null
        scope.launch {
            try {
                block()
            } catch (e: Exception) {
                _error.value = describeManualError((e as? SnaProtocolException)?.code, e.message)
            } finally {
                monitor.refresh()
                _busy.value = false
            }
        }
    }
}

internal fun describeManualError(code: String?, message: String?): String = when (code) {
    "output_active" -> "The source is routed to a port. Terminate the output (port 0) first."
    "tune_failed" -> "The synthesizer rejected that frequency."
    "wrong_mode" -> "The device is not in manual mode."
    else -> message ?: code ?: "Unknown error"
}
