package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlin.math.roundToLong

/**
 * What the generator is (or should be) putting out. [attenSteps] are 0.25 dB steps below maximum.
 * [freqHz] is null only for a running output whose frequency the device did not report.
 */
data class GeneratorOutput(val port: Int, val z: Impedance, val attenSteps: Int, val freqHz: Long?)

/** "1000", "1000.5" or "1000,5" (MHz) -> Hz, or null if it isn't a number inside the supported band. */
fun parseFrequencyMhz(text: String): Long? {
    val mhz = text.trim().replace(',', '.').toDoubleOrNull()?.takeIf { it.isFinite() } ?: return null
    if (mhz > FrequencyLimits.MAX_HZ / 1e6) return null
    val hz = (mhz * 1e6).roundToLong()
    return hz.takeIf { it in FrequencyLimits.MIN_HZ..FrequencyLimits.MAX_HZ }
}

/**
 * CW generator control. On/off is never remembered locally: [active] is derived from the
 * device's own `status` (via [monitor]), because the front-panel button, a USB disconnect and
 * the firmware's own interlocks can all switch the generator off behind the client's back.
 */
class GeneratorState(
    private val client: SnaClient,
    private val scope: CoroutineScope,
    private val monitor: DeviceMonitor,
) {
    /** The output currently on, or null when the generator is off. */
    val active: StateFlow<GeneratorOutput?> = monitor.status.map { status ->
        if (status?.gen_on != true) null else GeneratorOutput(
            status.port,
            Impedance.fromOhms(status.z) ?: Impedance.FIFTY, // the protocol only knows 50 and 75
            status.atten,
            status.gen_f, // missing: unknown, not 0 Hz
        )
    }.stateIn(scope, SharingStarted.Eagerly, null)

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    /** Holds the device's generator mode from the first start until [stop]. */
    fun start(output: GeneratorOutput) = command {
        client.acquireMode(DeviceMode.GENERATOR, this)
        try {
            client.genStart(output.port, output.z, output.attenSteps,
                requireNotNull(output.freqHz) { "No output frequency given" })
        } catch (e: Exception) {
            client.releaseMode(this) // a failed start leaves the output off
            throw e
        }
    }

    fun stop() = command {
        try {
            client.genStop()
        } finally {
            client.releaseMode(this)
        }
    }

    /** Changes only the level of a running output, without interrupting it. */
    fun setLevel(attenSteps: Int) = command { client.set(atten = attenSteps) }

    /** Switches the impedance of a running output, without interrupting it (hot switching). */
    fun setImpedance(z: Impedance) = command { client.set(z = z) }

    private fun command(block: suspend () -> Unit) {
        if (!_busy.compareAndSet(false, true)) return
        _error.value = null
        scope.launch {
            try {
                block()
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _error.value = describeGeneratorError((e as? SnaProtocolException)?.code ?: e.message)
            } finally {
                monitor.refresh() // success or failure: show what the device really did
                _busy.value = false
            }
        }
    }
}

internal fun describeGeneratorError(code: String?): String = when (code) {
    "source_not_locked" -> "The synthesizer did not lock at that frequency; the output stays off."
    "busy" -> "The device is busy with a sweep. Abort it first."
    "wrong_mode", "mode_active" -> "The device is busy with another tool. Stop it first."
    "bad_args" -> "The device rejected the settings."
    else -> code ?: "Unknown error"
}
