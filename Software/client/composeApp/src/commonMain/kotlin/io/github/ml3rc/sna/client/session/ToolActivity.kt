package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.protocol.DeviceMode

/** The instrument's tabs, as far as they run (or are locked out by) something on the device. */
enum class Tool { SWEEP, GENERATOR, POWER, CALIBRATION, SDI, DIAGNOSTICS, CONSOLE }

/**
 * Everything that decides what is running on the device and what is locked out right now. The
 * client's own tools report their state directly; [deviceMode], [deviceSweeping] and [deviceGenOn]
 * come from the device's `status`, and [ownedMode] is the mode this client's tools hold.
 */
data class ToolActivity(
    val sweeping: Boolean = false,
    /** A raw detector sweep (the Sweep tab's second view) is running; it occupies the device like a sweep. */
    val rawSweeping: Boolean = false,
    val calibrating: Boolean = false,
    val powerReading: Boolean = false,
    /** The detector probe is polling. */
    val probing: Boolean = false,
    /** The mode the client's tools hold (null: none). */
    val ownedMode: DeviceMode? = null,
    /** `status.mode`; null for a mode this client does not know. */
    val deviceMode: DeviceMode? = DeviceMode.IDLE,
    /** `status.sweeping`: a sweep or the on-board self-test runs. */
    val deviceSweeping: Boolean = false,
    /** `status.gen_on`. */
    val deviceGenOn: Boolean = false,
) {
    private val ownSweep: Boolean get() = sweeping || rawSweeping || calibrating

    /** The on-board self-test: the device sweeps, but none of this client's tools started it. */
    val onBoardTestBusy: Boolean get() = deviceSweeping && !ownSweep

    /** The generator this client switched on. */
    val generatorOn: Boolean get() = deviceGenOn && ownedMode == DeviceMode.GENERATOR

    /** Manual RF control (Diagnostics) holds the RF blocks. */
    val manualControl: Boolean get() = ownedMode == DeviceMode.MANUAL

    /** The device is in a mode none of this client's tools holds: entered from the console. */
    val foreignMode: DeviceMode?
        get() = deviceMode?.takeIf { it != DeviceMode.IDLE && ownedMode == null && !ownSweep && !onBoardTestBusy }

    fun isRunning(tool: Tool): Boolean = when (tool) {
        Tool.SWEEP -> sweeping || rawSweeping
        Tool.CALIBRATION -> calibrating
        Tool.GENERATOR -> generatorOn || probing
        Tool.POWER -> powerReading
        Tool.SDI -> onBoardTestBusy
        Tool.DIAGNOSTICS -> manualControl
        Tool.CONSOLE -> foreignMode != null
    }

    /** Something runs on the device, whoever started it. */
    val busy: Boolean get() = Tool.entries.any { isRunning(it) }

    val label: String
        get() = when {
            sweeping -> "Sweeping"
            rawSweeping -> "Raw detector sweep"
            calibrating -> "Calibrating"
            generatorOn -> "Generator on"
            powerReading -> "Reading power"
            probing -> "Probe reading"
            onBoardTestBusy -> "On-board test running"
            manualControl -> "Manual RF control"
            foreignMode != null -> "${foreignMode!!.label.replaceFirstChar { it.uppercase() }} mode (console)"
            else -> "Connected"
        }
}

/**
 * The interlock. Only one thing may drive the RF blocks at a time, and a tool that would silently
 * take over another is refused instead. Returns why [tool] cannot act right now, or null if it can
 * (also when it is the one running). The readouts of Diagnostics and the console log are never
 * locked; this gates their actions (manual control, bootloader, sending).
 *
 * A mode entered from the console only locks out tools that need another mode: a tool whose
 * mode the device is already in adopts it.
 */
fun blockReason(tool: Tool, a: ToolActivity): String? {
    val foreign = a.foreignMode
    val reasons = listOf(
        // Owner null: locks out every tool, also the self-test settings (EEPROM commands answer busy during it).
        Triple(a.onBoardTestBusy, null, "A on-board self-test is running on the device."),
        Triple(a.sweeping, Tool.SWEEP, "A sweep is running."),
        Triple(a.rawSweeping, Tool.SWEEP, "A raw detector sweep is running."),
        Triple(a.calibrating, Tool.CALIBRATION, "A calibration capture is running."),
        Triple(a.generatorOn, Tool.GENERATOR, "The generator is on. Turn it off first."),
        Triple(a.probing, Tool.GENERATOR, "The detector probe is reading. Stop it first."),
        Triple(a.powerReading, Tool.POWER, "The power meter is reading. Stop it first."),
        Triple(a.manualControl, Tool.DIAGNOSTICS, "Manual RF control is active (Diagnostics). Leave it first."),
        Triple(foreign != null && foreign != tool.mode, Tool.CONSOLE,
            "The device is in ${foreign?.label} mode, entered from the console. Send mode_exit first."),
    )
    return reasons.firstOrNull { (active, owner, _) -> active && owner != tool }?.third
}

/** The device mode a tool works in, if any. */
private val Tool.mode: DeviceMode?
    get() = when (this) {
        Tool.SWEEP, Tool.CALIBRATION -> DeviceMode.SWEEP
        Tool.GENERATOR -> DeviceMode.GENERATOR
        Tool.POWER -> DeviceMode.POWER_METER
        Tool.DIAGNOSTICS -> DeviceMode.MANUAL
        Tool.SDI, Tool.CONSOLE -> null
    }
