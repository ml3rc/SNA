package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.protocol.CalCurve
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.CalSet
import io.github.ml3rc.sna.client.protocol.DeviceInfo
import io.github.ml3rc.sna.client.protocol.DeviceStatus
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SdiTestPointConfig
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import io.github.ml3rc.sna.client.protocol.SweepPoint
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.launch
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonObjectBuilder
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.add
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.decodeFromJsonElement
import kotlinx.serialization.json.double
import kotlinx.serialization.json.int
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray

/**
 * Speaks the SNA wire protocol (see `Software/protocol/PROTOCOL.md`) over a [SerialTransport]:
 * one JSON object per line, request/response correlated by `id`, plus unsolicited `event` lines
 * and multi-line streamed responses (sweep points) for a given `id`.
 *
 * Call [start] once the transport is open, [stop] before closing it.
 */
class SnaClient(private val transport: SerialTransport, private val scope: CoroutineScope) {

    private val json = Json { ignoreUnknownKeys = true }
    private val nextId = MutableStateFlow(0)
    private val pending = MutableStateFlow<Map<Int, CompletableDeferred<JsonObject>>>(emptyMap())
    private val streams = MutableStateFlow<Map<Int, Channel<JsonObject>>>(emptyMap())
    private val writes = Mutex()
    private val _failure = MutableStateFlow<Throwable?>(null)
    val failure = _failure.asStateFlow()

    private val _events = MutableSharedFlow<JsonObject>(extraBufferCapacity = 16)
    /** Unsolicited device events, e.g. the standalone SDI self-test result -- see PROTOCOL.md. */
    val events: SharedFlow<JsonObject> = _events.asSharedFlow()

    private var readerJob: Job? = null

    fun start() {
        check(readerJob == null) { "Client already started" }
        readerJob = scope.launch {
            try {
                transport.incomingLines().collect { line -> handleLine(line) }
                fail(SnaProtocolException("Device disconnected"))
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                fail(e)
            }
        }
    }

    fun stop() {
        fail(SnaProtocolException("Connection closed"))
        readerJob?.cancel()
    }

    private fun fail(cause: Throwable) {
        _failure.compareAndSet(null, cause)
        pending.value.values.forEach { it.completeExceptionally(cause) }
        streams.value.values.forEach { it.close(cause) }
    }

    private suspend fun write(payload: JsonObject) = writes.withLock {
        _failure.value?.let { throw it }
        transport.writeLine(payload.toString())
    }

    private suspend fun handleLine(line: String) {
        val trimmed = line.trim()
        if (trimmed.isEmpty()) return
        val obj = runCatching { json.parseToJsonElement(trimmed).jsonObject }.getOrNull() ?: return

        if ("event" in obj) {
            _events.tryEmit(obj)
            return
        }
        val id = (obj["id"] as? JsonPrimitive)?.intOrNull ?: return

        streams.value[id]?.let { channel ->
            // One complete legal sweep fits in the buffer. Never suspend the shared reader:
            // an abandoned collector must not prevent abort/status responses being read.
            val sent = channel.trySend(obj)
            if (sent.isFailure && !sent.isClosed) {
                channel.close(SnaProtocolException("Sweep response buffer overflow"))
                streams.update { it - id }
            }
            if ("done" in obj || (obj["ok"] as? JsonPrimitive)?.booleanOrNull == false) {
                channel.close()
                streams.update { it - id }
            }
            return
        }
        pending.value[id]?.complete(obj)
    }

    private fun allocId(): Int {
        while (true) {
            val previous = nextId.value
            val next = if (previous == Int.MAX_VALUE) 1 else previous + 1
            if (nextId.compareAndSet(previous, next)) return next
        }
    }

    private suspend fun request(build: JsonObjectBuilder.(id: Int) -> Unit): JsonObject {
        val id = allocId()
        val deferred = CompletableDeferred<JsonObject>()
        pending.update { it + (id to deferred) }
        try {
            return withTimeout(10_000) {
                write(buildJsonObject { build(id) })
                deferred.await()
            }
        } finally {
            pending.update { it - id }
        }
    }

    private fun JsonObject.requireOk(): JsonObject {
        val ok = (this["ok"] as? JsonPrimitive)?.booleanOrNull ?: false
        if (!ok) {
            val err = (this["error"] as? JsonPrimitive)?.content ?: "unknown_error"
            throw SnaProtocolException(err)
        }
        return this
    }

    suspend fun ping(): DeviceInfo {
        val resp = request { id -> put("cmd", "ping"); put("id", id) }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    suspend fun status(): DeviceStatus {
        val resp = request { id -> put("cmd", "status"); put("id", id) }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    suspend fun setImpedance(z: Impedance) {
        request { id -> put("cmd", "set_z"); put("id", id); put("z", z.ohms) }.requireOk()
    }

    /**
     * Triggers a sweep and returns a flow of points as they're measured, completing when the
     * device reports `done`. Cancelling collection does NOT abort the sweep on-device -- call
     * [abort] explicitly if the user cancels.
     */
    fun sweep(
        f0Hz: Long,
        f1Hz: Long,
        points: Int,
        log: Boolean,
        z: Impedance,
        params: Set<SParam>,
        allowUnlocked: Boolean = true,
        attenuation: Int? = null,
    ): Flow<SweepPoint> = flow {
        FrequencyLimits.requireRange(f0Hz, f1Hz)
        require(points in 1..4096) { "Sweep points must be in 1..4096" }
        require(params.isNotEmpty()) { "Select at least one S-parameter" }
        if (attenuation != null) {
            require(attenuation in 0..127) { "Attenuation must be in 0..127" }
            check(ping().sweep_atten) { "Maximum-level calibration requires firmware 0.1.6 or newer" }
        }
        val id = allocId()
        val channel = Channel<JsonObject>(capacity = points + 1)
        streams.update { it + (id to channel) }
        val payload = buildJsonObject {
            put("cmd", "sweep"); put("id", id)
            put("f0", f0Hz); put("f1", f1Hz); put("points", points); put("log", log)
            put("z", z.ohms)
            put("allow_unlocked", allowUnlocked)
            attenuation?.let { put("atten", it) }
            putJsonArray("params") { params.forEach { add(it.wireName) } }
        }
        var received = 0
        try {
            write(payload)
            while (true) {
                val obj = withTimeout(60_000) { channel.receive() }
                if ("done" in obj) {
                    val ok = (obj["ok"] as? JsonPrimitive)?.booleanOrNull ?: false
                    if (!ok) {
                        val error = (obj["error"] as? JsonPrimitive)?.content ?: "sweep_failed"
                        val frequency = (obj["f"] as? JsonPrimitive)?.content
                        val stage = (obj["stage"] as? JsonPrimitive)?.content
                        val point = (obj["pt"] as? JsonPrimitive)?.content
                        val message = if (error == "source_not_locked" && frequency != null) {
                            "Source not locked at $frequency Hz ($stage, point $point)"
                        } else error
                        throw SnaProtocolException(message)
                    }
                    check(received == points) { "Incomplete sweep: received $received of $points points" }
                    break
                }
                if ((obj["ok"] as? JsonPrimitive)?.booleanOrNull == false) obj.requireOk()
                val point = json.decodeFromJsonElement<SweepPoint>(obj)
                check(point.pt == received && received < points) { "Invalid or missing sweep point at index $received" }
                received++
                emit(point)
            }
        } finally {
            streams.update { it - id }
            channel.cancel()
        }
    }

    suspend fun abort() {
        request { id -> put("cmd", "abort"); put("id", id) }.requireOk()
    }

    suspend fun calSet(set: CalSet, curve: CalCurve) {
        FrequencyLimits.requireRange(curve.f0, curve.f1)
        require(curve.points.size in 1..240 && curve.points.all { it.isFinite() }) {
            "Calibration needs 1..240 finite points"
        }
        request { id ->
            put("cmd", "cal_set"); put("id", id); put("set", set.wireName)
            put("f0", curve.f0); put("f1", curve.f1)
            putJsonArray("points") { curve.points.forEach { add(it) } }
        }.requireOk()
    }

    /** Returns null if that set has not been calibrated yet (device responds `not_calibrated`). */
    suspend fun calGet(set: CalSet): CalCurve? {
        val resp = request { id -> put("cmd", "cal_get"); put("id", id); put("set", set.wireName) }
        val ok = (resp["ok"] as? JsonPrimitive)?.booleanOrNull ?: false
        if (!ok && (resp["error"] as? JsonPrimitive)?.content == "not_calibrated") return null
        resp.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    suspend fun calClear(set: CalSet? = null) {
        request { id -> put("cmd", "cal_clear"); put("id", id); put("set", set?.wireName ?: "all") }.requireOk()
    }

    suspend fun sdiTestSet(points: List<SdiTestPointConfig>) {
        require(points.size <= 32 && points.all { it.thresh_db.isFinite() }) { "SDI needs at most 32 finite thresholds" }
        require(points.all { it.f in FrequencyLimits.MIN_HZ..FrequencyLimits.SDI_MAX_HZ }) {
            "SDI frequencies must be within 50 MHz–4294967295 Hz"
        }
        request { id ->
            put("cmd", "sdi_test_set"); put("id", id)
            putJsonArray("points") {
                points.forEach { p ->
                    add(buildJsonObject {
                        put("f", p.f); put("thresh_db", p.thresh_db)
                        put("s_param", p.s_param); put("cmp", p.cmp)
                    })
                }
            }
        }.requireOk()
    }

    suspend fun sdiTestGet(): List<SdiTestPointConfig> {
        val resp = request { id -> put("cmd", "sdi_test_get"); put("id", id) }.requireOk()
        val arr = resp["points"]?.jsonArray ?: return emptyList()
        return arr.map { json.decodeFromJsonElement(it) }
    }

    /** Direct RF-block control for bring-up/bench diagnostics -- not used during normal sweeps.
     *  `block` in {"atten","lpf","cplr","port","z","src_freq","src_en"}, see PROTOCOL.md. */
    suspend fun manual(block: String, value: Int) {
        request { id -> put("cmd", "manual"); put("id", id); put("block", block); put("value", value) }.requireOk()
    }

    /** MAX2871 internal temperature sensor, for bring-up/thermal monitoring -- see
     *  PROTOCOL.md's `src_temp_read`. Returns null if the device reports `not_valid`
     *  (e.g. the source isn't enabled/locked), same nullable-on-known-failure idiom as [calGet]. */
    suspend fun srcTempRead(): Double? {
        val resp = request { id -> put("cmd", "src_temp_read"); put("id", id) }
        if ((resp["ok"] as? JsonPrimitive)?.booleanOrNull != true &&
            (resp["error"] as? JsonPrimitive)?.content == "not_valid") return null
        resp.requireOk()
        return (resp["temp_c"] as JsonPrimitive).double
    }

    suspend fun adcRead(): Pair<Int, Double> {
        val resp = request { id -> put("cmd", "adc_read"); put("id", id) }.requireOk()
        val raw = (resp["raw"] as JsonPrimitive).int
        val volts = (resp["volts"] as JsonPrimitive).double
        return raw to volts
    }

    /** Reboots the device into its USB mass-storage (UF2) bootloader for firmware updates. The
     *  device disconnects immediately after acknowledging -- callers should expect [SerialTransport.close] on
     *  the transport to follow shortly after this returns. */
    suspend fun bootsel() {
        request { id -> put("cmd", "bootsel"); put("id", id) }.requireOk()
    }
}
