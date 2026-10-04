package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceStatus
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Bench housekeeping readback: device status, MAX2871 temperature, raw detector ADC. Manual
 * refresh only (not auto-polled) -- firing adc_read/src_temp_read while a sweep is in progress
 * (driven from the Sweep tab) risks colliding with the device's busy state.
 */
private class HkState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val _status = MutableStateFlow<DeviceStatus?>(null)
    val status: StateFlow<DeviceStatus?> = _status.asStateFlow()

    private val _tempC = MutableStateFlow<Double?>(null)
    val tempC: StateFlow<Double?> = _tempC.asStateFlow()

    private val _tempFetched = MutableStateFlow(false)
    val tempFetched: StateFlow<Boolean> = _tempFetched.asStateFlow()

    private val _adc = MutableStateFlow<Pair<Int, Double>?>(null)
    val adc: StateFlow<Pair<Int, Double>?> = _adc.asStateFlow()

    private val _refreshing = MutableStateFlow(false)
    val refreshing: StateFlow<Boolean> = _refreshing.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    fun refreshAll() {
        scope.launch {
            _refreshing.value = true
            runCatching {
                _status.value = client.status()
                _tempC.value = client.srcTempRead()
                _tempFetched.value = true
                _adc.value = client.adcRead()
            }.onFailure { _error.value = it.message }
            _refreshing.value = false
        }
    }
}

@Composable
fun HkScreen(viewModel: SnaViewModel) {
    val client = viewModel.client ?: return
    val deviceInfo by viewModel.deviceInfo.collectAsState()
    val coroutineScope = rememberCoroutineScope()
    val hk = remember(client) { HkState(client, coroutineScope) }

    val status by hk.status.collectAsState()
    val tempC by hk.tempC.collectAsState()
    val tempFetched by hk.tempFetched.collectAsState()
    val adc by hk.adc.collectAsState()
    val refreshing by hk.refreshing.collectAsState()
    val error by hk.error.collectAsState()

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(14.dp)) {
        Text("Housekeeping", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))

        Panel(title = "Device") {
            LabeledValue("Firmware", deviceInfo?.fw ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Hardware rev", deviceInfo?.hw_rev?.toString() ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Protocol", deviceInfo?.proto?.toString() ?: "—")
        }

        Spacer(Modifier.height(8.dp))
        Panel(title = "Live readings") {
            LabeledValue("State", status?.state ?: "—")
            Spacer(Modifier.height(6.dp))
            LabeledValue("Impedance", status?.z?.let { "${it}Ω" } ?: "—")
            status?.last_error?.let {
                Spacer(Modifier.height(6.dp))
                LabeledValue("Last error", it)
            }
            Spacer(Modifier.height(6.dp))
            LabeledValue(
                "MAX2871 temp",
                when {
                    !tempFetched -> "— (not read yet)"
                    tempC != null -> "${tempC}°C"
                    else -> "not_valid (source off)"
                },
            )
            Spacer(Modifier.height(6.dp))
            LabeledValue("Detector ADC", adc?.let { (raw, volts) -> "raw=$raw  ${volts}V" } ?: "— (not read yet)")

            Spacer(Modifier.height(8.dp))
            Row {
                Button(enabled = !refreshing, onClick = { hk.refreshAll() }) {
                    Text(if (refreshing) "Refreshing…" else "Refresh")
                }
            }
            error?.let { Spacer(Modifier.height(8.dp)); Text(it, color = MaterialTheme.colorScheme.error) }
        }
    }
}
