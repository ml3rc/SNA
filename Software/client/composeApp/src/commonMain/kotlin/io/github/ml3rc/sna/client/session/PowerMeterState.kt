package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.PowerReading
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Continuous power-meter reading: the device streams the readings (`power_start`), this class only
 * holds the power-meter mode and shows them. It runs in the connection's scope, not in a screen, so it keeps
 * going (and keeps its menu indicator) when another tab is open; [Session] stops it when
 * something else needs the RF blocks.
 */
class PowerMeterState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val _reading = MutableStateFlow<PowerReading?>(null)
    val reading: StateFlow<PowerReading?> = _reading.asStateFlow()

    private val _running = MutableStateFlow(false)
    val running: StateFlow<Boolean> = _running.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    private val _port = MutableStateFlow(1)
    val port: StateFlow<Int> = _port.asStateFlow()

    private val _z = MutableStateFlow(Impedance.FIFTY)
    val z: StateFlow<Impedance> = _z.asStateFlow()

    // start() runs on the UI thread, the interlock's stop() on the connection's dispatcher.
    private val lock = Any()
    private var job: Job? = null
    private var generation = 0

    private fun isCurrent(run: Int) = synchronized(lock) { run == generation }

    /** Changes the input settings; a running reading restarts with them. */
    fun select(port: Int, z: Impedance) {
        _port.value = port
        _z.value = z
        if (_running.value) start()
    }

    fun start() = start(_port.value, _z.value)

    /** (Re)starts reading with the given settings. A device error stops it and is shown in [error]. */
    fun start(port: Int, z: Impedance, intervalMs: Int = 250) {
        _port.value = port
        _z.value = z
        synchronized(lock) {
            job?.cancel()
            val mine = ++generation
            _error.value = null
            _reading.value = null
            _running.value = true
            val holder = Any() // this run's hold on the power-meter mode
            job = scope.launch {
                try {
                    client.acquireMode(DeviceMode.POWER_METER, holder)
                    // Leaving the mode (releaseMode below) also ends the device's stream.
                    client.powerStream(port, z, intervalMs.coerceIn(20, 10_000)).collect { value ->
                        if (isCurrent(mine)) _reading.value = value
                    }
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    if (isCurrent(mine)) _error.value = describePowerError((e as? SnaProtocolException)?.code, e.message)
                } finally {
                    client.releaseMode(holder)
                    if (isCurrent(mine)) _running.value = false
                }
            }
        }
    }

    fun stop() = synchronized(lock) {
        generation++
        job?.cancel()
        job = null
        _running.value = false
        _reading.value = null
    }
}

/** Readable text for a device error [code]; anything else (e.g. [SnaClient.acquireMode]'s own "in generator mode") keeps its [message]. */
internal fun describePowerError(code: String?, message: String?): String = when (code) {
    "wrong_mode" -> "The device is busy with another tool. Stop it first."
    "busy" -> "The device is busy with a sweep."
    "power_stream_stalled" -> "The device stopped sending power readings."
    else -> message ?: code ?: "Unknown error"
}

/**
 * The detector sits at a constant output (about 2.11 V) with no input, which the dBm formula would
 * extrapolate to roughly -66 dBm. Show such readings as a bound instead of a number that looks measured.
 */
internal fun formatDetectorDbm(dbm: Double, range: String): String = when {
    range == "high" -> "> 0 dBm"
    range == "low" && dbm < -60.0 -> "< -60 dBm"
    else -> "%.1f dBm".format(dbm)
}
