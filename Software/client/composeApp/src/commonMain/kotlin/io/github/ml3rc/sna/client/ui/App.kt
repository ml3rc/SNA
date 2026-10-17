package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.session.AppState
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import io.github.ml3rc.sna.client.transport.SerialTransport
import io.github.ml3rc.sna.client.ui.theme.*

private enum class Screen(val title: String, val compactTitle: String, val tool: Tool) {
    SWEEP("Sweep", "Sweep", Tool.SWEEP), GENERATOR("Generator", "Gen", Tool.GENERATOR),
    POWER("Power meter", "Power", Tool.POWER), CALIBRATION("Calibration", "Calibrate", Tool.CALIBRATION),
    SDI("Self-test", "SDI", Tool.SDI), DIAGNOSTICS("Diagnostics", "System", Tool.DIAGNOSTICS),
    CONSOLE("Console", "Console", Tool.CONSOLE),
}

@Composable
fun App(createTransport: () -> SerialTransport) {
    val app = remember { AppState(createTransport()) }
    DisposableEffect(app) { onDispose { app.close() } }
    App(app)
}

@Composable
fun App(app: AppState, compactUi: Boolean = false, modifier: Modifier = Modifier) {
    BoxWithConstraints(modifier.fillMaxSize()) {
        val compact = compactUi || maxWidth < 720.dp || maxHeight < 720.dp
        val rail = maxWidth >= 1000.dp && !compact
        MaterialTheme(colorScheme = SnaDarkColorScheme,
            typography = if (compact) SnaCompactTypography else SnaTypography, shapes = SnaShapes) {
            val session by app.session.collectAsState()
            var screenName by rememberSaveable { mutableStateOf(Screen.SWEEP.name) }
            Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                val current = session
                if (current == null) ConnectScreen(app, onConnected = { screenName = Screen.SWEEP.name })
                else Workspace(app, current, Screen.entries.firstOrNull { it.name == screenName } ?: Screen.SWEEP,
                    { screenName = it.name }, compact, rail)
            }
        }
    }
}

@Composable
private fun Workspace(app: AppState, session: Session, screen: Screen, select: (Screen) -> Unit, compact: Boolean, rail: Boolean) {
    val activity = rememberActivity(session)
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.fillMaxWidth().background(MaterialTheme.colorScheme.surface)
            .padding(horizontal = if (compact) 12.dp else 20.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Column(Modifier.weight(1f)) {
                Text(if (compact) "SNA" else "SNA · Scalar network analyzer", style = MaterialTheme.typography.titleMedium)
                Text("Firmware ${session.info.fw}  ·  ${activity.label}",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            OutlinedButton(onClick = { app.disconnect() }) { Text("Disconnect") }
        }
        Row(Modifier.fillMaxWidth().height(1.dp).background(MaterialTheme.colorScheme.outlineVariant)) {}
        val navigation: @Composable (Boolean) -> Unit = { vertical ->
            Screen.entries.forEach { item ->
                val selected = screen == item
                Column((if (vertical) Modifier.fillMaxWidth() else Modifier.widthIn(min = 86.dp))
                    .background(if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface)
                    .selectable(selected, role = Role.Tab, onClick = { select(item) })
                    .padding(horizontal = 14.dp, vertical = 14.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(if (compact) item.compactTitle else item.title, style = MaterialTheme.typography.labelLarge,
                            color = if (selected) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
                        // Green square: this tool is running on the device right now.
                        if (activity.isRunning(item.tool)) Box(Modifier.size(8.dp).background(ChartColors.Running)
                            .testTag("tool-running-${item.name}"))
                    }
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
                        Screen.SWEEP -> SweepScreen(session, compact)
                        Screen.GENERATOR -> GeneratorScreen(session, compact)
                        Screen.POWER -> PowerMeterScreen(session, compact)
                        Screen.CALIBRATION -> CalibrationScreen(session, compact)
                        Screen.SDI -> SdiScreen(session, compact)
                        Screen.DIAGNOSTICS -> DiagnosticsScreen(app, session, compact)
                        Screen.CONSOLE -> ConsoleScreen(session, compact)
                    }
                }
            }
        }
    }
}
