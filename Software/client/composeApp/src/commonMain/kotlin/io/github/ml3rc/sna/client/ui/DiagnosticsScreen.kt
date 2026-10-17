package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.session.AppState
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.session.blockReason
import io.github.ml3rc.sna.client.session.parseFrequencyMhz

/**
 * Housekeeping readback (refreshed continuously, never locked out), bench control of single RF
 * blocks, and the firmware-update reboot.
 */
@Composable
fun DiagnosticsScreen(app: AppState, session: Session, compactUi: Boolean = false) {
    val hk = session.housekeeping
    LaunchedEffect(hk) { hk.poll() }

    val status by session.monitor.status.collectAsState()
    val activity = rememberActivity(session)
    val temp by hk.temp.collectAsState()
    val tempFetched by hk.tempFetched.collectAsState()
    val error by hk.error.collectAsState()
    val info = session.info

    ScreenColumn("Housekeeping", compactUi, "Updates continuously and stays available while a sweep or the generator is running.") {
        Panel(title = "Device", modifier = Modifier.fillMaxWidth()) {
            LabeledValue("Firmware", info.fw)
            Spacer(Modifier.height(6.dp))
            LabeledValue("Hardware rev", info.hw_rev.toString())
            Spacer(Modifier.height(6.dp))
            LabeledValue("Protocol", info.proto.toString())
            Spacer(Modifier.height(6.dp))
            LabeledValue("Frequency range", "${info.freq_min_hz / 1_000_000} MHz – ${info.freq_max_hz / 1_000_000} MHz")
        }

        Spacer(Modifier.height(8.dp))
        Panel(title = "Tools", modifier = Modifier.fillMaxWidth()) {
            Tool.entries.forEachIndexed { index, tool ->
                if (index > 0) Spacer(Modifier.height(6.dp))
                LabeledValue(toolTitle(tool), when {
                    activity.isRunning(tool) -> "running"
                    blockReason(tool, activity) != null -> "locked out"
                    else -> "idle"
                })
            }
        }

        Spacer(Modifier.height(8.dp))
        Panel(title = "Live readings", modifier = Modifier.fillMaxWidth()) {
            LabeledValue("Mode", status?.mode ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Sweeping", status?.let { if (it.sweeping) "yes" else "no" } ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Impedance", status?.z?.let { "$it Ω" } ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Source port", status?.port?.toString() ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Attenuator code", status?.atten?.let { "$it (${formatAtten(it)})" } ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Generator", status?.let { s -> if (s.gen_on) "on" + (s.gen_f?.let { " · ${formatFrequency(it)}" } ?: "") else "off" } ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Power meter", status?.let { s -> if (s.pwr_on) "on · port ${s.pwr_port ?: "?"}" else "off" } ?: "—")
            // Boot self-test failures: the device may not measure correctly until they are fixed.
            status?.init_errors?.takeIf { it.isNotEmpty() }?.let { errors ->
                ErrorText("Boot self-test failed: " + errors.joinToString(", ") { describeInitError(it) }, "init-errors")
            }
            Spacer(Modifier.height(6.dp))
            LabeledValue("MAX2871 temp", when {
                !tempFetched -> "— (not read yet)"
                temp != null -> "${temp!!.tempC}°C" + if (temp!!.cached) " (last live reading, ${temp!!.ageMs / 1000}s ago)" else ""
                else -> "no valid reading yet"
            })
            Hint("The sensor tracks the VCO state; only trust it at a fixed frequency.")
            ErrorText(error)
        }

        Spacer(Modifier.height(8.dp))
        ManualPanel(session)

        Spacer(Modifier.height(8.dp))
        BootloaderPanel(app, session)
    }
}

/** Bench control of single RF blocks (`manual`). */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ManualPanel(session: Session) {
    val manual = session.manual
    val active by manual.active.collectAsState()
    val settings by manual.settings.collectAsState()
    val busy by manual.busy.collectAsState()
    val error by manual.error.collectAsState()
    val probe by manual.probe.collectAsState()
    val gate = rememberGate(session, Tool.DIAGNOSTICS)
    // While manual control holds the device, every control here acts on it right away.
    val live = active && !busy
    var freqMhz by remember { mutableStateOf("1000") }
    val freqHz = parseFrequencyMhz(freqMhz)
    val routed = settings.sourceOn == true

    Panel(title = "Manual RF control", modifier = Modifier.fillMaxWidth().testTag("manual-panel")) {
        Hint("Drives single RF blocks for bench checks. Every other tool is locked out meanwhile. Leaving terminates the output " +
            "and leaves the source running and parked. Sweeps and the generator select filter and coupler path themselves.")
        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(enabled = !active && !busy && !gate.locked, onClick = { manual.enter() }) { Text("Enter manual control") }
            OutlinedButton(enabled = active && !busy, onClick = { manual.exit() }) { Text("Leave manual control") }
        }
        if (!active) ErrorText(gate.reason)
        if (active) {
            Spacer(Modifier.height(8.dp))
            Text("Output", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ChoiceChip("Terminated", settings.port == 0, live) { manual.setPort(0) }
                ChoiceChip("Port 1", settings.port == 1, live) { manual.setPort(1) }
                ChoiceChip("Port 2", settings.port == 2, live) { manual.setPort(2) }
                ChoiceChip("Source on", settings.sourceEnabled == true, live) { manual.setSourceEnabled(true) }
                ChoiceChip("Source off", settings.sourceEnabled == false, live) { manual.setSourceEnabled(false) }
            }
            settings.sourceOn?.let { Hint(if (it) "The source is routed to the port." else "The output is terminated (or the source is off).") }
            Spacer(Modifier.height(8.dp))
            // The device refuses these while the source is routed to a port; terminate first.
            val routeEditable = live && !routed
            Text("Low-pass filter", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("None", "3500 MHz", "1747 MHz", "947 MHz").forEachIndexed { i, label ->
                    ChoiceChip(label, settings.lpf == i, routeEditable) { manual.setLpf(i) }
                }
            }
            Spacer(Modifier.height(8.dp))
            Text("Coupler path (detector)", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                listOf("Port 1 fwd", "Port 1 bwd", "Port 2 fwd", "Port 2 bwd").forEachIndexed { i, label ->
                    ChoiceChip(label, settings.coupler == i, routeEditable) { manual.setCoupler(i) }
                }
            }
            Spacer(Modifier.height(8.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                NumberField(freqMhz, { freqMhz = it }, "Synthesizer (MHz)", Modifier.weight(1f), routeEditable, decimal = true)
                Spacer(Modifier.width(8.dp))
                OutlinedButton(enabled = routeEditable && freqHz != null, onClick = { freqHz?.let(manual::setSourceFrequency) }) { Text("Tune") }
            }
            settings.sourceFreqHz?.let { Hint("Tuned to ${formatFrequency(it)}.") }
            if (routed) Hint("Filter, coupler path and frequency need the output terminated.")
            Spacer(Modifier.height(8.dp))
            OutlinedButton(enabled = live, onClick = { manual.readDetector() }) { Text("Read detector") }
            probe?.let { ProbeTable(it, showHints = false) }
        }
        ErrorText(error, "manual-error")
    }
}

@Composable
private fun BootloaderPanel(app: AppState, session: Session) {
    val gate = rememberGate(session, Tool.DIAGNOSTICS)
    val activity = rememberActivity(session)
    var confirming by remember { mutableStateOf(false) }
    val enabled = !activity.busy && !gate.locked
    Panel(title = "Firmware update", modifier = Modifier.fillMaxWidth()) {
        Hint("Reboots the analyzer into the RP2350's UF2 bootloader; it then shows up as a USB drive for the new firmware file.")
        Spacer(Modifier.height(6.dp))
        if (!confirming) OutlinedButton(enabled = enabled, onClick = { confirming = true }) { Text("Reboot to bootloader") }
        else Column {
            Text("The connection ends and the device stops measuring until new firmware is copied. Continue?")
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button(enabled = enabled, onClick = { confirming = false; app.rebootToBootloader() }) { Text("Reboot") }
                TextButton(onClick = { confirming = false }) { Text("Cancel") }
            }
        }
        if (activity.busy) Hint("Not while a test runs.")
    }
}

/** `status.init_errors` entries, see PROTOCOL.md `status`. */
private fun describeInitError(code: String): String = when (code) {
    "eeprom" -> "EEPROM not readable or writable"
    "source_lock" -> "synthesizer did not lock"
    "detector" -> "detector reading implausible"
    else -> code
}

internal fun toolTitle(tool: Tool): String = when (tool) {
    Tool.SWEEP -> "Sweep"
    Tool.GENERATOR -> "Generator"
    Tool.POWER -> "Power meter"
    Tool.CALIBRATION -> "Calibration"
    Tool.SDI -> "Self-test (front panel)"
    Tool.DIAGNOSTICS -> "Manual RF control"
    Tool.CONSOLE -> "Console (mode entered by hand)"
}
