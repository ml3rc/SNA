package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SdiCmp
import io.github.ml3rc.sna.client.protocol.SdiTestPointConfig
import io.github.ml3rc.sna.client.protocol.SdiTestResultEvent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.decodeFromJsonElement

/** One editable threshold row; texts stay strings so half-typed values survive. [key] identifies the row in lists. */
data class SdiRow(
    val key: Long,
    val fHz: String = "",
    val sParam: SParam = SParam.S11,
    val threshDb: String = "",
    val cmp: SdiCmp = SdiCmp.LESS_THAN,
)

/**
 * The on-board SDI self-test: the threshold list being edited, loading/uploading it, and the
 * last `sdi_test_result` the device reported. Owned by the session, so edits survive tab changes.
 */
class SdiState(private val client: SnaClient, private val scope: CoroutineScope) {
    private val json = Json { ignoreUnknownKeys = true }
    private var nextKey = 0L

    private val _rows = MutableStateFlow<List<SdiRow>>(emptyList())
    val rows: StateFlow<List<SdiRow>> = _rows.asStateFlow()

    private val _busy = MutableStateFlow(false)
    val busy: StateFlow<Boolean> = _busy.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    private val _message = MutableStateFlow<String?>(null)
    /** The outcome of the last load/upload. */
    val message: StateFlow<String?> = _message.asStateFlow()

    private val _lastResult = MutableStateFlow<SdiTestResultEvent?>(null)
    /** The latest on-board test result while connected. */
    val lastResult: StateFlow<SdiTestResultEvent?> = _lastResult.asStateFlow()

    init {
        scope.launch {
            client.events.collect { event ->
                if ((event["event"] as? JsonPrimitive)?.content != "sdi_test_result") return@collect
                runCatching { json.decodeFromJsonElement<SdiTestResultEvent>(event) }.onSuccess { _lastResult.value = it }
            }
        }
    }

    fun addRow() {
        if (_rows.value.size >= ProtocolLimits.SDI_POINTS_MAX) return
        _rows.update { it + SdiRow(nextKey++) }
    }

    fun removeRow(key: Long) = _rows.update { rows -> rows.filterNot { it.key == key } }

    fun updateRow(key: Long, change: (SdiRow) -> SdiRow) = _rows.update { rows -> rows.map { if (it.key == key) change(it) else it } }

    /** Replaces the rows with the device's stored list. An unknown value fails the load and leaves the rows as they were. */
    fun load() = command {
        val loaded = client.sdiTestGet().map { p ->
            SdiRow(
                key = nextKey++,
                fHz = p.f.toString(),
                sParam = SParam.fromWire(p.s_param) ?: throw IllegalArgumentException("The device returned an unknown S-parameter \"${p.s_param}\"."),
                threshDb = p.thresh_db.toString(),
                cmp = SdiCmp.fromWire(p.cmp) ?: throw IllegalArgumentException("The device returned an unknown comparison \"${p.cmp}\"."),
            )
        }
        _rows.value = loaded
        _message.value = if (loaded.isEmpty()) "The device has no stored points (test disabled)." else "Loaded ${loaded.size} points."
    }

    /** Uploads the rows (an empty list disables the test). */
    fun upload() {
        val parsed = _rows.value.map { row ->
            val f = row.fHz.trim().toLongOrNull()
            val t = row.threshDb.trim().replace(',', '.').toDoubleOrNull()
            if (f == null || t == null) {
                _error.value = "Invalid frequency or threshold value in one or more rows"
                return
            }
            SdiTestPointConfig(f = f, thresh_db = t, s_param = row.sParam.wireName, cmp = row.cmp.wireName)
        }
        command {
            client.sdiTestSet(parsed)
            _message.value = if (parsed.isEmpty()) "Test disabled on the device." else "Uploaded ${parsed.size} points."
        }
    }

    private fun command(block: suspend () -> Unit) {
        if (!_busy.compareAndSet(false, true)) return
        _error.value = null
        _message.value = null
        scope.launch {
            runCatching { block() }.onFailure { _error.value = it.message }
            _busy.value = false
        }
    }
}
