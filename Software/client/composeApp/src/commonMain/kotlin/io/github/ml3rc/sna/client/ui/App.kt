package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.selection.selectable
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.transport.SerialTransport
import io.github.ml3rc.sna.client.ui.theme.*

private enum class Screen(val title: String, val compactTitle: String) {
    SWEEP("Sweep", "Sweep"), CALIBRATION("Calibration", "Calibrate"), SDI("Self-test", "SDI"), HK("Diagnostics", "System")
}

@Composable
fun App(createTransport: () -> SerialTransport) {
    val viewModel = remember { SnaViewModel(createTransport()) }
    DisposableEffect(viewModel) { onDispose { viewModel.close() } }
    App(viewModel)
}

@Composable
fun App(viewModel: SnaViewModel, compactUi: Boolean = false, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier.fillMaxSize()) {
        val compact = compactUi || maxWidth < 720.dp || maxHeight < 720.dp
        val rail = maxWidth >= 1000.dp && !compact
        MaterialTheme(colorScheme = SnaDarkColorScheme,
            typography = if (compact) SnaCompactTypography else SnaTypography, shapes = SnaShapes) {
            val connected by viewModel.connected.collectAsState()
            val sweeping by viewModel.sweeping.collectAsState()
            val deviceInfo by viewModel.deviceInfo.collectAsState()
            var screenIndex by rememberSaveable { mutableStateOf(Screen.SWEEP.ordinal) }
            val screen = Screen.entries[screenIndex]
            Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                if (!connected) {
                    ConnectScreen(viewModel, onConnected = { screenIndex = Screen.SWEEP.ordinal })
                } else {
                    Column(Modifier.fillMaxSize()) {
                        Row(Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface)
                            .padding(horizontal = if (compact) 12.dp else 20.dp, vertical = 8.dp),
                            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                            Column(Modifier.weight(1f)) {
                                Text(if (compact) "SNA" else "SNA · Scalar network analyzer", style = MaterialTheme.typography.titleMedium)
                                Text("Firmware ${deviceInfo?.fw ?: "—"}  ·  ${if (sweeping) "Acquiring" else "Connected"}",
                                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                            OutlinedButton(onClick = { viewModel.disconnect() }) { Text("Disconnect") }
                        }
                        Row(Modifier.fillMaxWidth().height(1.dp).background(MaterialTheme.colorScheme.outlineVariant)) {}
                        val navigation: @Composable (Boolean) -> Unit = { vertical ->
                            Screen.entries.forEach { item ->
                                val selected = screen == item
                                Column((if (vertical) Modifier.fillMaxWidth() else Modifier.widthIn(min = 86.dp))
                                    .background(if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface)
                                    .selectable(selected, role = Role.Tab, onClick = { screenIndex = item.ordinal })
                                    .padding(horizontal = 14.dp, vertical = 14.dp)) {
                                    Text(if (compact) item.compactTitle else item.title, style = MaterialTheme.typography.labelLarge,
                                        color = if (selected) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
                                    if (!vertical) {
                                        Spacer(Modifier.height(5.dp))
                                        Box(Modifier.height(2.dp).width(24.dp)
                                            .background(if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surface))
                                    }
                                }
                            }
                        }
                        Row(Modifier.weight(1f)) {
                            if (rail) {
                                Column(Modifier.width(156.dp).fillMaxHeight().background(MaterialTheme.colorScheme.surface)
                                    .verticalScroll(rememberScrollState()).padding(8.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                                    navigation(true)
                                }
                            }
                            Column(Modifier.weight(1f).fillMaxHeight()) {
                                if (!rail) Row(Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface)
                                    .horizontalScroll(rememberScrollState())) { navigation(false) }
                                Box(Modifier.weight(1f).fillMaxWidth()) {
                                    when (screen) {
                                        Screen.SWEEP -> SweepScreen(viewModel, compactUi = compact)
                                        Screen.CALIBRATION -> CalibrationScreen(viewModel, compactUi = compact)
                                        Screen.SDI -> SdiConfigScreen(viewModel)
                                        Screen.HK -> HkScreen(viewModel)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
