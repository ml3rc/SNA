package io.github.ml3rc.sna.client.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.session.ToolActivity
import io.github.ml3rc.sna.client.session.blockReason

/**
 * Bench measurement (docs-local/generator-and-power-meter.md, "Measured output level"): at 2 GHz the port level is about
 * -attenuation dBm (0 dB ≈ 0 dBm); it rises towards low frequencies (up to about +9-10 dB) and falls above 2 GHz.
 */
const val OUTPUT_LEVEL_NOTE = "Output level varies over frequency: about −attenuation dBm at 2 GHz (0 dB ≈ 0 dBm), " +
    "up to about +10 dB higher at low frequencies and lower above 2 GHz. Expect roughly ±10 dB."

/** Hz -> a compact frequency axis label. */
fun formatFreqAxisValue(hz: Double): String = when {
    hz >= 1e9 -> "%.3f GHz".format(hz / 1e9)
    hz >= 1e6 -> "%.1f MHz".format(hz / 1e6)
    hz >= 1e3 -> "%.0f kHz".format(hz / 1e3)
    else -> "%.0f Hz".format(hz)
}

/** "1 GHz", "2.4 GHz", "433.92 MHz" -- trailing zeros trimmed, either decimal separator (default locale). */
internal fun formatFrequency(hz: Long): String {
    val ghz = hz >= 1_000_000_000L
    val number = "%.6f".format(if (ghz) hz / 1e9 else hz / 1e6).trimEnd('0').trimEnd('.', ',')
    return "$number ${if (ghz) "GHz" else "MHz"}"
}

/** Attenuator steps as the relative level they set, e.g. "-16.00 dB". */
internal fun formatAtten(steps: Int): String = if (steps == 0) "0.00 dB" else "-%.2f dB".format(steps * 0.25)

/** Live interlock state of [session]. */
@Composable
fun rememberActivity(session: Session): ToolActivity {
    val activity by session.activity.collectAsState()
    return activity
}

/**
 * Whether [tool]'s controls may be used right now: [locked] when something else runs on the device
 * (its reason in [reason]), [running] while the tool's own test runs. A control is enabled when
 * the tool is not locked, and while it runs only if the control changes the running test live.
 */
data class ToolGate(val reason: String?, val running: Boolean) {
    val locked: Boolean get() = reason != null
    /** For settings that only take effect when the test is started. */
    val idleEditable: Boolean get() = !locked && !running
    /** For controls that act on the running test as well (live level, stop). */
    val liveEditable: Boolean get() = !locked
}

@Composable
fun rememberGate(session: Session, tool: Tool): ToolGate {
    val activity = rememberActivity(session)
    return ToolGate(blockReason(tool, activity), activity.isRunning(tool))
}
