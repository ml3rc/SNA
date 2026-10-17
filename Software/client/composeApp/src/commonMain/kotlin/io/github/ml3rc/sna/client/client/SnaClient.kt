package io.github.ml3rc.sna.client.client

import io.github.ml3rc.sna.client.protocol.CalCurve
import io.github.ml3rc.sna.client.protocol.CalSet
import io.github.ml3rc.sna.client.protocol.DetectorPath
import io.github.ml3rc.sna.client.protocol.DeviceInfo
import io.github.ml3rc.sna.client.protocol.DeviceMode
import io.github.ml3rc.sna.client.protocol.DeviceSettings
import io.github.ml3rc.sna.client.protocol.DeviceStatus
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.PowerReading
import io.github.ml3rc.sna.client.protocol.ProbeResult
import io.github.ml3rc.sna.client.protocol.ProtocolLimits
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SdiTestPointConfig
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import io.github.ml3rc.sna.client.protocol.SweepPoint
import io.github.ml3rc.sna.client.protocol.TempReading
import io.github.ml3rc.sna.client.transport.SerialTransport
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.withTimeoutOrNull
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonObjectBuilder
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.add
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.decodeFromJsonElement
import kotlinx.serialization.json.doubleOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.longOrNull
import kotlinx.serialization.json.put
import kotlinx.serialization.json.putJsonArray

/**
 * Speaks the SNA wire protocol (see `Software/docs/firmware/protocol/PROTOCOL.md`) over a [SerialTransport]:
 * one JSON object per line, request/response correlated by `id`, plus unsolicited `event` lines
 * and multi-line streamed responses (sweep points) for a given `id`. Every line in both directions
 * is recorded in [traffic].
 *
 * Call [start] once the transport is open, [stop] before closing it.
 */
class SnaClient(private val transport: SerialTransport, private val scope: CoroutineScope) {

    private class Pending(val reply: CompletableDeferred<JsonObject>, val kind: TrafficKind)

    private val json = Json { ignoreUnknownKeys = true }
    private val nextId = MutableStateFlow(0)
    private val pending = MutableStateFlow<Map<Int, Pending>>(emptyMap())
    private val streams = MutableStateFlow<Map<Int, Channel<JsonObject>>>(emptyMap())
    private val writes = Mutex()
    private var consoleSeq = 0
    private val _failure = MutableStateFlow<Throwable?>(null)
    /** Set once the link is dead (device gone, read error, [stop]); every later request fails with it. */
    val failure = _failure.asStateFlow()

    /** Every line sent and received, for the console. */
    val traffic = TrafficLog()

    private val _events = MutableSharedFlow<JsonObject>(extraBufferCapacity = 16)
    /** Unsolicited device events (`power`, `sdi_test_result`) -- see PROTOCOL.md. */
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
        pending.value.values.forEach { it.reply.completeExceptionally(cause) }
        streams.value.values.forEach { it.close(cause) }
    }

    private suspend fun write(payload: JsonObject, kind: TrafficKind) = writeLine(payload.toString(), kind)

    private suspend fun writeLine(line: String, kind: TrafficKind) = writes.withLock {
        _failure.value?.let { throw it }
        traffic.add(TrafficDirection.TX, kind, line)
        transport.writeLine(line)
    }

    private suspend fun handleLine(line: String) {
        val trimmed = line.trim()
        if (trimmed.isEmpty()) return
        val obj = runCatching { json.parseToJsonElement(trimmed).jsonObject }.getOrNull()
        if (obj == null) {
            traffic.add(TrafficDirection.RX, TrafficKind.UNPARSED, trimmed)
            return
        }
        if ("event" in obj) {
            traffic.add(TrafficDirection.RX, TrafficKind.EVENT, trimmed)
            _events.tryEmit(obj)
            return
        }
        val idValue = obj["id"] as? JsonPrimitive
        val id = idValue?.takeIf { !it.isString }?.intOrNull
        if (id == null) {
            // Only the console uses non-integer ids (see [sendRaw]).
            traffic.add(TrafficDirection.RX, if (idValue?.isString == true) TrafficKind.CONSOLE else TrafficKind.COMMAND, trimmed)
            return
        }

        streams.value[id]?.let { channel ->
            val final = "done" in obj || (obj["ok"] as? JsonPrimitive)?.booleanOrNull == false
            traffic.add(TrafficDirection.RX, if (final) TrafficKind.COMMAND else TrafficKind.SWEEP_POINT, trimmed)
            // One complete legal sweep fits in the buffer. Never suspend the shared reader:
            // an abandoned collector must not prevent abort/status responses being read.
            val sent = channel.trySend(obj)
            if (sent.isFailure && !sent.isClosed) {
                channel.close(SnaProtocolException("Sweep response buffer overflow"))
                streams.update { it - id }
            }
            if (final) {
                channel.close()
                streams.update { it - id }
            }
            return
        }
        val waiting = pending.value[id]
        traffic.add(TrafficDirection.RX, waiting?.kind ?: TrafficKind.COMMAND, trimmed)
        waiting?.reply?.complete(obj)
    }

    private fun allocId(): Int {
        while (true) {
            val previous = nextId.value
            val next = if (previous == Int.MAX_VALUE) 1 else previous + 1
            if (nextId.compareAndSet(previous, next)) return next
        }
    }

    private suspend fun request(kind: TrafficKind = TrafficKind.COMMAND, build: JsonObjectBuilder.(id: Int) -> Unit): JsonObject {
        val id = allocId()
        val deferred = CompletableDeferred<JsonObject>()
        pending.update { it + (id to Pending(deferred, kind)) }
        try {
            return withTimeout(10_000) {
                write(buildJsonObject { build(id) }, kind)
                deferred.await()
            }
        } finally {
            pending.update { it - id }
        }
    }

    private fun JsonObject.isOk(): Boolean = (this["ok"] as? JsonPrimitive)?.booleanOrNull ?: false

    private fun JsonObject.errorCode(): String? = (this["error"] as? JsonPrimitive)?.content

    private fun JsonObject.requireOk(): JsonObject {
        if (!isOk()) throw deviceError()
        return this
    }

    /** The error of a failed reply: its code, and for `bad_args` the offending field. */
    private fun JsonObject.deviceError(): SnaProtocolException {
        val code = errorCode() ?: "unknown_error"
        val field = (this["field"] as? JsonPrimitive)?.content
        return SnaProtocolException(if (field != null) "$code ($field)" else code, code, field)
    }

    /**
     * Sends one line typed into the console. A JSON object without an `id` gets a string id
     * (`"console-N"`), which can never collide with this client's integer ids, so its reply shows up
     * in [traffic] without completing anything else. Anything else is sent as typed. Returns the line sent.
     */
    suspend fun sendRaw(text: String): String {
        val trimmed = text.replace("\r", "").replace("\n", " ").trim()
        require(trimmed.isNotEmpty()) { "Nothing to send" }
        val parsed = runCatching { json.parseToJsonElement(trimmed) }.getOrNull()
        val line = if (parsed is JsonObject && "id" !in parsed) buildJsonObject {
            parsed.forEach { (key, value) -> put(key, value) }
            put("id", "console-${++consoleSeq}")
        }.toString() else trimmed
        writeLine(line, TrafficKind.CONSOLE)
        return line
    }

    suspend fun ping(): DeviceInfo {
        val resp = request { id -> put("cmd", "ping"); put("id", id) }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    suspend fun status(): DeviceStatus {
        val resp = request(TrafficKind.POLL) { id -> put("cmd", "status"); put("id", id) }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    /**
     * Changes impedance and/or attenuation (`set`, at least one of them). Works in every mode and
     * during a sweep: [z] switches immediately, also under a live output; [atten] (0.25 dB steps
     * below maximum) changes a running output live and a running sweep from its next point on.
     * Returns the resulting settings.
     */
    suspend fun set(z: Impedance? = null, atten: Int? = null): DeviceSettings {
        require(z != null || atten != null) { "Nothing to set" }
        require(atten == null || atten in ProtocolLimits.ATTEN) { "Attenuation must be in ${ProtocolLimits.ATTEN}" }
        val resp = request { id ->
            put("cmd", "set"); put("id", id)
            z?.let { put("z", it.ohms) }
            atten?.let { put("atten", it) }
        }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    /**
     * Triggers a sweep and returns a flow of points as they're measured, completing when the
     * device reports `done`. Cancelling collection does NOT abort the sweep on-device -- call
     * [abort] explicitly if the user cancels (or leave the mode, see [releaseMode]).
     */
    fun sweep(
        f0Hz: Long,
        f1Hz: Long,
        points: Int,
        log: Boolean,
        z: Impedance,
        params: Set<SParam>,
        attenuation: Int,
        allowUnlocked: Boolean = true,
    ): Flow<SweepPoint> {
        require(params.isNotEmpty()) { "Select at least one S-parameter" }
        return streamSweep(f0Hz, f1Hz, points, log, z, params.map { it.wireName }, allowUnlocked, attenuation, null)
    }

    /**
     * The normal sweep, but the "parameters" are the detector's coupler states: each point carries the
     * uncalibrated reading (`p1_fwd_raw` / `p1_fwd_dbm`, ...) of the requested [paths] with the
     * source on at [port]. Same acquisition, settling and lock handling as an S-parameter sweep.
     */
    fun sweepRaw(
        f0Hz: Long,
        f1Hz: Long,
        points: Int,
        log: Boolean,
        z: Impedance,
        port: Int,
        paths: Set<DetectorPath>,
        attenuation: Int,
    ): Flow<SweepPoint> {
        require(paths.isNotEmpty()) { "Select at least one detector state" }
        require(port in 1..2) { "Source port must be 1 or 2" }
        return streamSweep(f0Hz, f1Hz, points, log, z, paths.map { it.wireName }, true, attenuation, port)
    }

    private fun streamSweep(
        f0Hz: Long,
        f1Hz: Long,
        points: Int,
        log: Boolean,
        z: Impedance,
        paramNames: List<String>,
        allowUnlocked: Boolean,
        attenuation: Int,
        port: Int?,
    ): Flow<SweepPoint> = flow {
        FrequencyLimits.requireRange(f0Hz, f1Hz)
        require(points in ProtocolLimits.SWEEP_POINTS) { "Sweep points must be in ${ProtocolLimits.SWEEP_POINTS}" }
        require(attenuation in ProtocolLimits.ATTEN) { "Attenuation must be in ${ProtocolLimits.ATTEN}" }
        val id = allocId()
        val channel = Channel<JsonObject>(capacity = points + 1)
        streams.update { it + (id to channel) }
        val payload = buildJsonObject {
            put("cmd", "sweep"); put("id", id)
            put("f0", f0Hz); put("f1", f1Hz); put("points", points); put("log", log)
            // z, atten and params are required: every sweep brings its own impedance and level.
            put("z", z.ohms); put("atten", attenuation)
            put("allow_unlocked", allowUnlocked)
            putJsonArray("params") { paramNames.forEach { add(it) } }
            port?.let { put("port", it) } // raw detector sweeps only (and required there): the source port
        }
        var received = 0
        try {
            write(payload, TrafficKind.COMMAND)
            while (true) {
                val obj = withTimeout(60_000) { channel.receive() }
                if ("done" in obj) {
                    if (!obj.isOk()) {
                        val error = obj.deviceError()
                        val frequency = (obj["f"] as? JsonPrimitive)?.content
                        val stage = (obj["stage"] as? JsonPrimitive)?.content
                        val point = (obj["pt"] as? JsonPrimitive)?.content
                        if (error.code == "source_not_locked" && frequency != null) throw SnaProtocolException(
                            "Source not locked at $frequency Hz ($stage, point $point)", error.code)
                        throw error
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

    /** Ends a running sweep (it then fails with `aborted`); `ok` also when nothing runs. */
    suspend fun abort() {
        request { id -> put("cmd", "abort"); put("id", id) }.requireOk()
    }

    /** Starts (or re-applies) the CW generator. [attenSteps] are 0.25 dB steps below maximum output. */
    suspend fun genStart(port: Int, z: Impedance, attenSteps: Int, freqHz: Long) {
        require(port in 1..2) { "Port must be 1 or 2" }
        require(attenSteps in ProtocolLimits.ATTEN) { "Attenuation must be in ${ProtocolLimits.ATTEN}" }
        FrequencyLimits.requireRange(freqHz, freqHz)
        request { id ->
            put("cmd", "gen_start"); put("id", id); put("port", port); put("z", z.ohms)
            put("atten", attenSteps); put("f", freqHz)
        }.requireOk()
    }

    suspend fun genStop() {
        request { id -> put("cmd", "gen_stop"); put("id", id) }.requireOk()
    }

    /** One power-meter reading of the signal arriving at [port]. Needs [DeviceMode.POWER_METER]. */
    suspend fun powerRead(port: Int, z: Impedance): PowerReading {
        require(port in 1..2) { "Port must be 1 or 2" }
        val resp = request { id -> put("cmd", "power_read"); put("id", id); put("port", port); put("z", z.ohms) }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    /**
     * Continuous power meter (`power_start`): the device sends a `power` event every [intervalMs]
     * until `power_stop` or `mode_exit`. Needs [DeviceMode.POWER_METER]. Collecting starts the
     * measurement; collecting again switches the running one to the new settings. Fails with
     * `power_stream_stalled` if the readings stop arriving.
     */
    fun powerStream(port: Int, z: Impedance, intervalMs: Int = 250): Flow<PowerReading> = flow {
        require(port in 1..2) { "Port must be 1 or 2" }
        require(intervalMs in 20..10_000) { "Interval must be in 20..10000 ms" }
        coroutineScope {
            val readings = Channel<PowerReading>(Channel.CONFLATED)
            // Subscribed before power_start, so the first reading is not missed.
            val listener = launch(start = CoroutineStart.UNDISPATCHED) {
                events.collect { event ->
                    if ((event["event"] as? JsonPrimitive)?.content != "power") return@collect
                    val reading = runCatching { json.decodeFromJsonElement<PowerReading>(event) }.getOrNull() ?: return@collect
                    // Readings with the previous settings can still be on their way.
                    if (reading.port == port && reading.z == z.ohms) readings.trySend(reading)
                }
            }
            try {
                request { id ->
                    put("cmd", "power_start"); put("id", id); put("port", port); put("z", z.ohms)
                    put("interval_ms", intervalMs)
                }.requireOk()
                val silenceMs = maxOf(2_000L, intervalMs * 4L)
                while (true) {
                    val reading = withTimeoutOrNull(silenceMs) { readings.receive() }
                        ?: throw SnaProtocolException("power_stream_stalled", code = "power_stream_stalled")
                    emit(reading)
                }
            } finally {
                listener.cancel()
            }
        }
    }

    /** Ends the continuous power meter; the device stays in [DeviceMode.POWER_METER]. */
    suspend fun powerStop() {
        request { id -> put("cmd", "power_stop"); put("id", id) }.requireOk()
    }

    /** Detector readout on coupler paths 0..3 (p1 fwd, p1 bwd, p2 fwd, p2 bwd) with the route held as set. */
    suspend fun probeRead(paths: Set<Int> = setOf(0, 1, 2, 3)): ProbeResult {
        require(paths.isNotEmpty() && paths.all { it in 0..3 }) { "Coupler paths must be 0..3" }
        val resp = request { id ->
            put("cmd", "probe_read"); put("id", id)
            putJsonArray("paths") { paths.sorted().forEach { add(it) } }
        }.requireOk()
        return json.decodeFromJsonElement(resp)
    }

    // --- Device modes ---
    // The device runs one RF mode at a time. This client's tools hold the mode they need; tools
    // needing the same mode (the generator and the probe) share it, and the device goes back to
    // idle once the last holder releases it. Holders are opaque tokens, one per running activity.
    private val modeLock = Mutex()
    private val heldMode = MutableStateFlow<DeviceMode?>(null)
    private val modeHolders = mutableSetOf<Any>()

    /** The mode this client's tools hold, or null. A device mode without a holder here was entered elsewhere (console). */
    val ownedMode: StateFlow<DeviceMode?> = heldMode.asStateFlow()

    /**
     * Puts the device into [mode] for [holder]. Waits up to 2 s for another tool of this client to
     * release a different mode (stopped tools release asynchronously), then fails with a readable
     * message if the device is still busy in another mode. A device already in [mode] without a
     * holder here (entered from the console) is adopted.
     */
    suspend fun acquireMode(mode: DeviceMode, holder: Any) {
        withTimeoutOrNull(2000) { heldMode.first { it == null || it == mode } }
        modeLock.withLock {
            val held = heldMode.value
            if (held == mode) {
                modeHolders += holder
                return
            }
            // A mode nobody holds any more is one whose mode_exit failed: try leaving it once more.
            if (held != null && modeHolders.isEmpty() && exitMode()) heldMode.value = null
            val stillHeld = heldMode.value
            if (stillHeld != null) throw SnaProtocolException("The device is in ${stillHeld.label} mode. Stop that first.")
            val resp = request { id -> put("cmd", "mode_enter"); put("id", id); put("mode", mode.wireName) }
            val current = (resp["mode"] as? JsonPrimitive)?.content
            if (!resp.isOk() && current != mode.wireName) { // already in this mode (left over) is fine
                val error = resp.deviceError()
                if (error.code == "mode_active") throw SnaProtocolException(
                    "The device is in ${DeviceMode.fromWire(current)?.label ?: current} mode. Stop that first.", error.code)
                throw error
            }
            heldMode.value = mode
            modeHolders += holder
        }
    }

    /** [holder] no longer needs its mode; the last holder takes the device back to idle. Never cancelled midway. */
    suspend fun releaseMode(holder: Any) = withContext(NonCancellable) {
        modeLock.withLock {
            if (!modeHolders.remove(holder) || modeHolders.isNotEmpty()) return@withLock
            // Only forget the mode once the device has really left it; otherwise the next
            // acquireMode retries the exit instead of assuming an idle device.
            if (exitMode()) heldMode.value = null
        }
    }

    /** Sends `mode_exit` until it is answered; true if the device confirmed it is idle. Call under [modeLock]. */
    private suspend fun exitMode(): Boolean {
        // mode_exit also aborts a running sweep and answers once the mode is left; a second
        // mode_exit while that is pending answers `busy`.
        repeat(100) {
            val resp = runCatching { request { id -> put("cmd", "mode_exit"); put("id", id) } }.getOrNull() ?: return false
            if (resp.isOk()) return true
            if (resp.errorCode() != "busy") return false
            delay(50)
        }
        return false
    }

    /** Runs [block] with the device in [mode], then releases it. */
    suspend fun <T> withMode(mode: DeviceMode, holder: Any = Any(), block: suspend () -> T): T {
        acquireMode(mode, holder)
        try {
            return block()
        } finally {
            releaseMode(holder)
        }
    }

    suspend fun calSet(set: CalSet, curve: CalCurve) {
        FrequencyLimits.requireRange(curve.f0, curve.f1)
        require(curve.points.size in ProtocolLimits.CAL_POINTS && curve.points.all { it.isFinite() && it in ProtocolLimits.DB }) {
            "Calibration needs ${ProtocolLimits.CAL_POINTS} finite points within ${ProtocolLimits.DB_MIN}..${ProtocolLimits.DB_MAX} dB"
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
        if (!resp.isOk() && resp.errorCode() == "not_calibrated") return null
        return json.decodeFromJsonElement(resp.requireOk())
    }

    /** Clears one set, or all 8 (which also releases the shared grid) when [set] is null. */
    suspend fun calClear(set: CalSet? = null) {
        request { id -> put("cmd", "cal_clear"); put("id", id); put("set", set?.wireName ?: "all") }.requireOk()
    }

    suspend fun sdiTestSet(points: List<SdiTestPointConfig>) {
        require(points.size <= ProtocolLimits.SDI_POINTS_MAX && points.all { it.thresh_db in ProtocolLimits.DB }) {
            "SDI needs at most ${ProtocolLimits.SDI_POINTS_MAX} thresholds within ${ProtocolLimits.DB_MIN}..${ProtocolLimits.DB_MAX} dB"
        }
        require(points.all { it.f in FrequencyLimits.MIN_HZ..FrequencyLimits.MAX_HZ }) {
            "SDI frequencies must be within 50 MHz–6 GHz"
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

    /**
     * Direct RF-block control for bench diagnostics (mode [DeviceMode.MANUAL]): [block] in
     * `lpf`, `cplr`, `port`, `src_freq` (see PROTOCOL.md). Returns `source_on` for `port`, else null.
     * Impedance and attenuation are [set].
     */
    suspend fun manual(block: String, value: Long): Boolean? {
        val resp = request { id -> put("cmd", "manual"); put("id", id); put("block", block); put("value", value) }.requireOk()
        return (resp["source_on"] as? JsonPrimitive)?.booleanOrNull
    }

    /** The boolean `manual` block: `src_en` (the RF source as a whole). Mode [DeviceMode.MANUAL]. */
    suspend fun manual(block: String, value: Boolean) {
        request { id -> put("cmd", "manual"); put("id", id); put("block", block); put("value", value) }.requireOk()
    }

    /** MAX2871 temperature sensor (`src_temp_read`). Null if the device has no valid reading (`not_valid`). */
    suspend fun srcTempRead(): TempReading? {
        val resp = request(TrafficKind.POLL) { id -> put("cmd", "src_temp_read"); put("id", id) }
        if (!resp.isOk() && resp.errorCode() == "not_valid") return null
        resp.requireOk()
        val tempC = (resp["temp_c"] as? JsonPrimitive)?.takeIf { !it.isString }?.doubleOrNull
            ?: throw SnaProtocolException("Malformed src_temp_read reply: no numeric temp_c")
        return TempReading(
            tempC = tempC,
            cached = (resp["cached"] as? JsonPrimitive)?.booleanOrNull ?: false,
            ageMs = (resp["age_ms"] as? JsonPrimitive)?.longOrNull ?: 0L,
        )
    }

    /** Reboots the device into its UF2 bootloader. It disconnects right after acknowledging. */
    suspend fun bootsel() {
        request { id -> put("cmd", "bootsel"); put("id", id) }.requireOk()
    }
}
