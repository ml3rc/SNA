package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import io.github.ml3rc.sna.client.client.TrafficDirection
import io.github.ml3rc.sna.client.client.TrafficEntry
import io.github.ml3rc.sna.client.client.TrafficKind
import io.github.ml3rc.sna.client.session.LIVE_UPDATE_MS
import io.github.ml3rc.sna.client.session.Session
import io.github.ml3rc.sna.client.session.Tool
import kotlinx.coroutines.delay

private val EXAMPLES = listOf(
    """{"cmd":"status"}""", """{"cmd":"ping"}""", """{"cmd":"src_temp_read"}""",
    """{"cmd":"mode_enter","mode":"manual"}""", """{"cmd":"mode_exit"}""",
)

/**
 * Every line the client exchanges with the device, and a prompt to send lines by hand. Sending is
 * only possible while none of the client's tools runs a test; a mode entered from here locks the
 * other tools out until `mode_exit` (the header shows it).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun ConsoleScreen(session: Session, compactUi: Boolean = false) {
    val console = session.console
    val input by console.input.collectAsState()
    val error by console.error.collectAsState()
    val gate = rememberGate(session, Tool.CONSOLE)
    val canSend = !gate.locked

    var showPolling by rememberSaveable { mutableStateOf(false) }
    var showPoints by rememberSaveable { mutableStateOf(false) }
    var follow by rememberSaveable { mutableStateOf(true) }
    var entries by remember { mutableStateOf(emptyList<TrafficEntry>()) }
    // Refresh at display rate, not per line: a loop sweep logs hundreds of lines per second, and
    // recomposing per line would cost more than the measurement. Newest first, for the reversed list.
    LaunchedEffect(console.log, showPolling, showPoints, follow) {
        var seen = -1L
        while (true) {
            val revision = console.log.revision.value
            if (revision != seen && follow) {
                seen = revision
                entries = console.log.snapshot().asReversed().filter { e ->
                    (showPolling || e.kind != TrafficKind.POLL) && (showPoints || e.kind != TrafficKind.SWEEP_POINT)
                }
            }
            delay(LIVE_UPDATE_MS)
        }
    }
    // The list is laid out bottom-up, so index 0 (the newest line) sits at the bottom edge however
    // tall the wrapped lines turn out; following just keeps it scrolled to 0.
    val listState = rememberLazyListState()
    LaunchedEffect(entries.firstOrNull()?.seq, follow) {
        if (follow && entries.isNotEmpty() && listState.firstVisibleItemIndex != 0) listState.scrollToItem(0)
    }

    Column(Modifier.fillMaxSize().padding(if (compactUi) 8.dp else 14.dp)) {
        Text("Protocol console", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(4.dp))
        Hint("Newline-delimited JSON as in docs/firmware/protocol/PROTOCOL.md. Leave out \"id\": the console adds one. " +
            "Up/Down recall earlier lines.")
        Spacer(Modifier.height(8.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            ToggleChip("Polling", showPolling, { showPolling = it })
            ToggleChip("Sweep points", showPoints, { showPoints = it })
            ToggleChip("Follow", follow, { follow = it })
            OutlinedButton(onClick = { console.log.clear() }) { Text("Clear") }
        }
        Spacer(Modifier.height(8.dp))
        Panel(modifier = Modifier.fillMaxWidth().weight(1f)) {
            if (entries.isEmpty()) Hint("Nothing yet.")
            SelectionContainer(Modifier.fillMaxSize()) {
                LazyColumn(Modifier.fillMaxSize().testTag("console-log"), state = listState, reverseLayout = true) {
                    items(entries, key = { it.seq }) { ConsoleLine(it) }
                }
            }
        }
        Spacer(Modifier.height(8.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(input, console::setInput, Modifier.weight(1f).testTag("console-input").onPreviewKeyEvent { event ->
                if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                when (event.key) {
                    Key.DirectionUp -> { console.recall(older = true); true }
                    Key.DirectionDown -> { console.recall(older = false); true }
                    Key.Enter, Key.NumPadEnter -> { if (canSend) console.send(); true }
                    else -> false
                }
            }, label = { Text("Command") }, enabled = canSend,
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Send),
                keyboardActions = KeyboardActions(onSend = { if (canSend) console.send() }))
            Spacer(Modifier.width(8.dp))
            Button(enabled = canSend && input.isNotBlank(), onClick = { console.send() }) { Text("Send") }
        }
        if (canSend) {
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                EXAMPLES.forEach { example ->
                    androidx.compose.material3.TextButton(onClick = { console.setInput(example) }) {
                        Text(example, fontFamily = FontFamily.Monospace, fontSize = 11.sp)
                    }
                }
            }
        }
        ErrorText(gate.reason?.let { "Sending is off while a test runs: $it" }, "console-blocked")
        ErrorText(error)
    }
}

@Composable
private fun ConsoleLine(entry: TrafficEntry) {
    val tx = entry.direction == TrafficDirection.TX
    val color = when {
        entry.kind == TrafficKind.UNPARSED -> MaterialTheme.colorScheme.error
        entry.kind == TrafficKind.EVENT -> MaterialTheme.colorScheme.secondary
        "\"ok\":false" in entry.text -> MaterialTheme.colorScheme.error
        tx -> MaterialTheme.colorScheme.primary
        else -> MaterialTheme.colorScheme.onSurface
    }
    Row(Modifier.fillMaxWidth().background(if (entry.kind == TrafficKind.CONSOLE)
        MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.35f) else MaterialTheme.colorScheme.surface)) {
        Text(formatClock(entry.timeMs), fontFamily = FontFamily.Monospace, fontSize = 11.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.width(92.dp))
        Text(if (tx) "→" else "←", fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = color, modifier = Modifier.width(18.dp))
        Text(entry.text, fontFamily = FontFamily.Monospace, fontSize = 12.sp, color = color)
    }
}

/** Local wall-clock time of day with milliseconds, e.g. "14:03:27.512". */
private fun formatClock(ms: Long): String {
    val zone = java.util.TimeZone.getDefault()
    val local = ms + zone.getOffset(ms)
    val dayMs = ((local % 86_400_000L) + 86_400_000L) % 86_400_000L
    return "%02d:%02d:%02d.%03d".format(dayMs / 3_600_000, dayMs / 60_000 % 60, dayMs / 1000 % 60, dayMs % 1000)
}
