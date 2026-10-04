package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SdiCmp
import io.github.ml3rc.sna.client.protocol.SdiTestPointConfig
import kotlinx.coroutines.launch

private class SdiRowState(
    fHz: String = "",
    sParam: SParam = SParam.S11,
    threshDb: String = "",
    cmp: SdiCmp = SdiCmp.LESS_THAN,
) {
    var fHz by mutableStateOf(fHz)
    var sParam by mutableStateOf(sParam)
    var threshDb by mutableStateOf(threshDb)
    var cmp by mutableStateOf(cmp)
}

private fun SParam.label() = wireName.uppercase()
private fun SdiCmp.label() = if (this == SdiCmp.LESS_THAN) "< (lt)" else "> (gt)"

/** Plain Box + OutlinedButton + DropdownMenu -- deliberately not Material3's
 *  "Exposed" dropdown family, whose exact API shape has moved between
 *  Compose Multiplatform versions (same class of issue flagged for Vico's
 *  chart API in SweepScreen.kt); this combo has been stable for a long time. */
@Composable
private fun <T> SimpleDropdown(label: String, selected: T, options: List<T>, optionLabel: (T) -> String, onSelect: (T) -> Unit, modifier: Modifier = Modifier) {
    var expanded by remember { mutableStateOf(false) }
    Box(modifier) {
        OutlinedButton(onClick = { expanded = true }, modifier = Modifier) {
            Text("$label: ${optionLabel(selected)}")
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            options.forEach { opt ->
                DropdownMenuItem(text = { Text(optionLabel(opt)) }, onClick = { onSelect(opt); expanded = false })
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SdiConfigScreen(viewModel: SnaViewModel) {
    val client = viewModel.client ?: return
    val coroutineScope = rememberCoroutineScope()
    val rows = remember { mutableStateListOf<SdiRowState>() }
    var error by remember { mutableStateOf<String?>(null) }
    var busy by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize().padding(14.dp)) {
        Text("SDI self-test thresholds", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(4.dp))
        Text(
            "Configures the standalone front-panel button test (works with no PC connected once uploaded).",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Spacer(Modifier.height(8.dp))

        Panel(title = "Thresholds", modifier = Modifier.weight(1f)) {
            if (rows.isEmpty()) {
                Text("No threshold points yet — click \"Add row\" below.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            LazyColumn(Modifier.weight(1f)) {
                items(rows) { row ->
                    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                      Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                        OutlinedTextField(
                            value = row.fHz, onValueChange = { row.fHz = it },
                            label = { Text("f (Hz)") }, modifier = Modifier.weight(1f),
                        )
                        Spacer(Modifier.width(8.dp))

                        OutlinedTextField(
                            value = row.threshDb, onValueChange = { row.threshDb = it },
                            label = { Text("thresh (dB)") }, modifier = Modifier.weight(1f),
                        )
                      }
                      Spacer(Modifier.height(6.dp))
                      FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        SimpleDropdown("S-param", row.sParam, SParam.entries, { it.label() }, { row.sParam = it })
                        SimpleDropdown("Compare", row.cmp, SdiCmp.entries, { it.label() }, { row.cmp = it })
                        TextButton(onClick = { rows.remove(row) }) { Text("Remove") }
                      }
                    }
                }
            }

            error?.let { Spacer(Modifier.height(8.dp)); Text(it, color = MaterialTheme.colorScheme.error) }
            Spacer(Modifier.height(8.dp))

            FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                OutlinedButton(onClick = { rows.add(SdiRowState()) }) { Text("Add row") }
                OutlinedButton(
                    enabled = !busy,
                    onClick = {
                        coroutineScope.launch {
                            busy = true
                            runCatching {
                                val loaded = client.sdiTestGet()
                                rows.clear()
                                loaded.forEach { p ->
                                    rows.add(SdiRowState(
                                        fHz = p.f.toString(),
                                        sParam = SParam.entries.first { it.wireName == p.s_param },
                                        threshDb = p.thresh_db.toString(),
                                        cmp = if (p.cmp == "gt") SdiCmp.GREATER_THAN else SdiCmp.LESS_THAN,
                                    ))
                                }
                            }.onFailure { error = it.message }
                            busy = false
                        }
                    },
                ) { Text("Load from device") }
                Button(
                    enabled = !busy && rows.isNotEmpty(),
                    onClick = {
                        val parsed = mutableListOf<SdiTestPointConfig>()
                        var allValid = true
                        for (row in rows) {
                            val f = row.fHz.toLongOrNull()
                            val t = row.threshDb.toDoubleOrNull()
                            if (f == null || t == null) {
                                allValid = false
                                break
                            }
                            parsed.add(SdiTestPointConfig(f = f, thresh_db = t, s_param = row.sParam.wireName, cmp = if (row.cmp == SdiCmp.GREATER_THAN) "gt" else "lt"))
                        }
                        if (!allValid) {
                            error = "Invalid frequency or threshold value in one or more rows"
                        } else {
                            coroutineScope.launch {
                                busy = true
                                runCatching { client.sdiTestSet(parsed) }
                                    .onFailure { error = it.message }
                                busy = false
                            }
                        }
                    },
                ) { Text("Upload to device") }
            }
        }
    }
}
