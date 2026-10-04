package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp

@Composable
fun ConnectScreen(viewModel: SnaViewModel, onConnected: () -> Unit) {
    val ports = viewModel.ports.collectAsState().value
    val connecting = viewModel.connecting.collectAsState().value
    val error = viewModel.error.collectAsState().value
    LaunchedEffect(Unit) { viewModel.refreshPorts() }
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val wide = maxWidth >= 800.dp
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(if (wide) 40.dp else 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally) {
            Column(Modifier.widthIn(max = 900.dp).fillMaxWidth()) {
                Text("SNA", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(if (wide) 48.dp else 24.dp))
                Text("Scalar network analyzer", style = MaterialTheme.typography.headlineLarge)
                Spacer(Modifier.height(8.dp))
                Text("50 MHz–6 GHz · USB instrument control", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(24.dp))
                Panel("USB connection", Modifier.fillMaxWidth()) {
                    if (connecting) {
                        Text("Connecting to analyzer…", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(8.dp))
                    }
                    if (ports.isEmpty()) {
                        Text("No SNA devices found", style = MaterialTheme.typography.titleMedium)
                        Spacer(Modifier.height(8.dp))
                        Text("Connect the analyzer with a USB data cable, then rescan.", color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                    ports.forEach { port ->
                        Surface(Modifier.fillMaxWidth().padding(bottom = 8.dp), shape = RoundedCornerShape(3.dp),
                            color = MaterialTheme.colorScheme.surfaceVariant,
                            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline)) {
                            Row(Modifier.clickable(enabled = !connecting, role = Role.Button,
                                onClick = { viewModel.connect(port, onConnected) }).padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text("SNA analyzer", style = MaterialTheme.typography.titleMedium)
                                    Spacer(Modifier.height(4.dp))
                                    Text(port.displayName, style = MaterialTheme.typography.bodyMedium,
                                        color = MaterialTheme.colorScheme.onSurfaceVariant)
                                }
                                Spacer(Modifier.width(12.dp))
                                Text("Connect", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
                            }
                        }
                    }
                    error?.let { Spacer(Modifier.height(8.dp)); Text(it, color = MaterialTheme.colorScheme.error) }
                    Spacer(Modifier.height(16.dp))
                    OutlinedButton(enabled = !connecting, onClick = { viewModel.refreshPorts() }) { Text("Rescan USB") }
                }
                Spacer(Modifier.height(24.dp))
                Text("Measurements", style = MaterialTheme.typography.titleSmall)
                Spacer(Modifier.height(8.dp))
                Text("Reflection (S11, S22) and transmission (S21, S12).", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.height(4.dp))
                Text("Calibration, standalone self-test and device diagnostics.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}