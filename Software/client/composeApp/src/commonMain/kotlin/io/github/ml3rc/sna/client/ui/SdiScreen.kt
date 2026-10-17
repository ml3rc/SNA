package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SdiCmp
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool

private fun SdiCmp.label() = if (this == SdiCmp.LESS_THAN) "< (lt)" else "> (gt)"

/**
 * Plain Box + OutlinedButton + DropdownMenu: Material3's "Exposed" dropdown family has changed
 * shape between Compose Multiplatform versions, this combination has been stable.
 */
@Composable
private fun <T> SimpleDropdown(label: String, selected: T, options: List<T>, optionLabel: (T) -> String,
    onSelect: (T) -> Unit, enabled: Boolean) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { expanded = true }, enabled = enabled) { Text("$label: ${optionLabel(selected)}") }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { opt ->
                DropdownMenuItem(text = { Text(optionLabel(opt)) }, onClick = { onSelect(opt); expanded = false })
            }
        }
    }
}

/** The on-board SDI self-test: its stored thresholds, and the last result the device reported. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SdiScreen(session: Session, compactUi: Boolean = false) {
    val sdi = session.sdi
    val rows by sdi.rows.collectAsState()
    val busy by sdi.busy.collectAsState()
    val error by sdi.error.collectAsState()
    val message by sdi.message.collectAsState()
    val lastResult by sdi.lastResult.collectAsState()
    val gate = rememberGate(session, Tool.SDI)
    // The thresholds live in the EEPROM, which the device only reads and writes while no test runs.
    val editable = gate.idleEditable && !busy

    ScreenColumn("SDI self-test thresholds", compactUi,
        "Configures the standalone on-board button test (works with no PC connected once uploaded). It measures at " +
            "maximum level on the 75 Ω ports, calibrated if data exists.") {
        Panel(title = "Thresholds (${rows.size} of ${ProtocolLimits.SDI_POINTS_MAX})", modifier = Modifier.fillMaxWidth()) {
            if (rows.isEmpty()) Hint("No threshold points yet — add a row, or load the device's list. Uploading an empty list disables the test.")
            rows.forEach { row ->
                Column(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        NumberField(row.fHz, { v -> sdi.updateRow(row.key) { it.copy(fHz = v) } }, "f (Hz)", Modifier.weight(1f), editable)
                        Spacer(Modifier.width(8.dp))
                        NumberField(row.threshDb, { v -> sdi.updateRow(row.key) { it.copy(threshDb = v) } }, "thresh (dB)", Modifier.weight(1f), editable, decimal = true)
                    }
                    Spacer(Modifier.height(6.dp))
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        SimpleDropdown("S-param", row.sParam, SParam.entries, { it.name }, { p -> sdi.updateRow(row.key) { it.copy(sParam = p) } }, editable)
                        SimpleDropdown("Compare", row.cmp, SdiCmp.entries, { it.label() }, { c -> sdi.updateRow(row.key) { it.copy(cmp = c) } }, editable)
                        TextButton(enabled = editable, onClick = { sdi.removeRow(row.key) }) { Text("Remove") }
                    }
                }
            }
            ErrorText(gate.reason)
            ErrorText(error)
            message?.let { Hint(it) }
            Spacer(Modifier.height(8.dp))
            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(enabled = editable && rows.size < ProtocolLimits.SDI_POINTS_MAX, onClick = { sdi.addRow() }) { Text("Add row") }
                OutlinedButton(enabled = editable, onClick = { sdi.load() }) { Text("Load from device") }
                Button(enabled = editable, onClick = { sdi.upload() }) { Text("Upload to device") }
            }
        }
        Spacer(Modifier.height(8.dp))
        Panel(title = "Last on-board result", modifier = Modifier.fillMaxWidth().testTag("sdi-result")) {
            val result = lastResult
            if (result == null) Hint("None since connecting. Press the SDI button on the device; the result shows up here.")
            else {
                Text(if (result.pass) "PASS" else "FAIL", style = MaterialTheme.typography.titleLarge, fontFamily = FontFamily.Monospace,
                    color = if (result.pass) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error)
                result.results.forEach { r ->
                    LabeledValue("${formatFrequency(r.f)} · ${r.s_param.uppercase()}", "%.2f dB · %s".format(r.val_db, if (r.pass) "pass" else "fail"))
                }
            }
        }
    }
}
