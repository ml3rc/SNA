package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.REQUIRED_PROTOCOL
import io.github.ml3rc.sna.client.transport.SerialPortInfo
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.cancel
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout

/**
 * The app's top-level state: port discovery, connecting (handshake and version check) and the
 * current [Session]. Plain coroutine-scope-backed class (no androidx ViewModel, so it lives in
 * commonMain); its owner calls [close] when finished. Android retains the owner across
 * configuration changes.
 */
class AppState(private val transport: SerialTransport) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var connectJob: Job? = null

    private val _session = MutableStateFlow<Session?>(null)
    /** The live connection, or null when disconnected. */
    val session: StateFlow<Session?> = _session.asStateFlow()

    val connected: StateFlow<Boolean> = _session.map { it != null }.stateIn(scope, SharingStarted.Eagerly, false)

    private val _ports = MutableStateFlow<List<SerialPortInfo>>(emptyList())
    val ports: StateFlow<List<SerialPortInfo>> = _ports.asStateFlow()

    private val _connecting = MutableStateFlow(false)
    val connecting: StateFlow<Boolean> = _connecting.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    /** Why the last connection attempt failed or the connection ended. */
    val error: StateFlow<String?> = _error.asStateFlow()

    fun refreshPorts() {
        scope.launch {
            runCatching { transport.listPorts() }
                .onSuccess { _ports.value = it }
                .onFailure { _error.value = it.message }
        }
    }

    fun connect(port: SerialPortInfo, onConnected: () -> Unit = {}) {
        // A second click while a connect is pending must not race a second transport.open()
        // against the first (seen on real hardware as a confusing "Failed to open COM4").
        if (_session.value != null || !_connecting.compareAndSet(false, true)) return
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
                check(info.proto == REQUIRED_PROTOCOL) {
                    "Firmware ${info.fw} speaks protocol ${info.proto}; this client needs protocol $REQUIRED_PROTOCOL " +
                        "(firmware 0.4.x). " + if (info.proto < REQUIRED_PROTOCOL) "Update the firmware." else "Update the client."
                }
                // Every range check, the grid and the calibration assume this band; a device reporting
                // another one would be driven with limits it does not have.
                check(info.freq_min_hz == FrequencyLimits.MIN_HZ && info.freq_max_hz == FrequencyLimits.MAX_HZ) {
                    "Firmware ${info.fw} reports a frequency range of ${info.freq_min_hz}–${info.freq_max_hz} Hz; this client " +
                        "supports exactly ${FrequencyLimits.MIN_HZ}–${FrequencyLimits.MAX_HZ} Hz. Update the client."
                }
                currentCoroutineContext().ensureActive()
                val session = Session(c, info, scope)
                _session.value = session
                succeeded = true
                scope.launch {
                    val failure = c.failure.filterNotNull().first()
                    if (_session.value === session) {
                        _error.value = failure.message
                        disconnect()
                    }
                }
            }.onSuccess {
                onConnected()
            }.onFailure {
                // Release the port on ANY failure, including a timed-out handshake after a
                // successful open, or every retry keeps failing until the app restarts.
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
        _connecting.value = false
        val old = _session.value
        _session.value = null
        old?.close()
        old?.client?.stop()
        transport.close()
    }

    /** Reboots the device into its UF2 bootloader for a firmware update, then drops the connection. */
    fun rebootToBootloader() {
        val session = _session.value ?: return
        scope.launch {
            val result = runCatching { session.client.bootsel() }
            // The device may drop off USB before its reply arrives; that counts as done too.
            if (result.isSuccess || _session.value !== session) {
                if (_session.value === session) disconnect()
                _error.value = "The device rebooted into its UF2 bootloader. Copy the new firmware onto the RP2350 drive, then reconnect."
            } else _error.value = result.exceptionOrNull()?.message
        }
    }

    /** Release the session when its owner finishes, not when a screen rotates. */
    fun close() {
        disconnect()
        scope.cancel()
    }
}
