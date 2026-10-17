package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.client.TrafficLog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * The protocol console: shows every line the client sends and receives ([log]), and sends lines
 * typed by the user while no test runs. Sending a mode change (`mode_enter`) makes the interlock
 * treat that mode as entered from the console; the status poll picks it up.
 */
class ConsoleState(private val client: SnaClient, private val scope: CoroutineScope, private val monitor: DeviceMonitor) {
    val log: TrafficLog get() = client.traffic

    private val _input = MutableStateFlow("")
    val input: StateFlow<String> = _input.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    private val history = mutableListOf<String>()
    private var historyIndex = 0

    fun setInput(text: String) { _input.value = text }

    /** Sends the input line. The caller checks the interlock (no test running). */
    fun send() {
        val text = _input.value.trim()
        if (text.isEmpty()) return
        _error.value = null
        if (history.lastOrNull() != text) history += text
        if (history.size > 50) history.removeAt(0)
        historyIndex = history.size
        _input.value = ""
        scope.launch {
            runCatching { client.sendRaw(text) }.onFailure { _error.value = it.message }
            // A mode entered or left from here changes the interlock: re-read the device state
            // once the command has had time to be answered.
            delay(200)
            monitor.refresh()
        }
    }

    /** Steps through earlier inputs; [older] goes back in time. */
    fun recall(older: Boolean) {
        if (history.isEmpty()) return
        historyIndex = (historyIndex + if (older) -1 else 1).coerceIn(0, history.size)
        _input.value = history.getOrNull(historyIndex) ?: ""
    }
}
