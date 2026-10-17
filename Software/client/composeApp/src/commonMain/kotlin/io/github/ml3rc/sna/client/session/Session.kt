package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceInfo
import io.github.ml3rc.sna.client.protocol.DeviceMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.drop
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch

/**
 * Everything that belongs to one connection: the protocol client, the device monitor, every
 * tool's state and the interlock between them. Created after a successful handshake, closed on
 * disconnect. Screens only read and drive this; nothing here is tied to a tab being open, so a
 * running sweep, capture or power reading keeps going while another tab is shown.
 */
class Session(val client: SnaClient, val info: DeviceInfo, parentScope: CoroutineScope) {
    private val job = SupervisorJob(parentScope.coroutineContext[Job])
    private val scope = CoroutineScope(parentScope.coroutineContext + job)

    val monitor = DeviceMonitor(client, scope)
    val sweep = SweepState(client, scope)
    val rawSweep = RawSweepState(client, scope)
    val generator = GeneratorState(client, scope, monitor)
    val power = PowerMeterState(client, scope)
    val probe = ProbeState(client, scope)
    val calibration = CalibrationWizardState(client, scope)
    val sdi = SdiState(client, scope)
    val manual = ManualState(client, scope, monitor)
    val housekeeping = HousekeepingState(client)
    val console = ConsoleState(client, scope, monitor)

    private data class OwnTools(val sweeping: Boolean, val raw: Boolean, val calibrating: Boolean, val power: Boolean, val probing: Boolean)

    private val ownTools = combine(sweep.running, rawSweep.running, calibration.capturing, power.running, probe.running) { s, r, c, p, pr ->
        OwnTools(s, r, c, p, pr)
    }.distinctUntilChanged()

    /** What runs right now, and so what is locked out (see [blockReason]). */
    val activity: StateFlow<ToolActivity> = combine(ownTools, client.ownedMode, monitor.status) { own, owned, status ->
        ToolActivity(
            sweeping = own.sweeping, rawSweeping = own.raw, calibrating = own.calibrating,
            powerReading = own.power, probing = own.probing, ownedMode = owned,
            deviceMode = if (status == null) DeviceMode.IDLE else DeviceMode.fromWire(status.mode),
            deviceSweeping = status?.sweeping == true, deviceGenOn = status?.gen_on == true,
        )
    }.stateIn(scope, SharingStarted.Eagerly, ToolActivity())

    init {
        monitor.start()
        // Never let a running reading collide with something else that started.
        scope.launch {
            activity.collect { a ->
                if (power.running.value && (a.sweeping || a.rawSweeping || a.calibrating || a.generatorOn || a.probing || a.manualControl)) power.stop()
                if (probe.running.value && (a.sweeping || a.rawSweeping || a.calibrating || a.powerReading || a.manualControl)) probe.stop()
            }
        }
        // Re-read the device state the moment a tool starts or stops or a mode is taken or released,
        // instead of up to a second later (and so without a stale "busy" in between).
        scope.launch {
            combine(ownTools, client.ownedMode) { own, owned -> own to owned }.distinctUntilChanged().drop(1).collect { monitor.refresh() }
        }
    }

    fun blockReason(tool: Tool): String? = blockReason(tool, activity.value)

    fun close() {
        power.stop()
        probe.stop()
        sweep.cancelLocal()
        rawSweep.cancelLocal()
        manual.cancelLocal()
        job.cancel()
    }
}
