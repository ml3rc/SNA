package io.github.ml3rc.sna.client.ui

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.FrequencyLimits
import io.github.ml3rc.sna.client.protocol.CalCurve
import io.github.ml3rc.sna.client.protocol.CalSet
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.SParam
import io.github.ml3rc.sna.client.protocol.SnaProtocolException
import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch

/**
 * The SLOT calibration math (see project memory, cross-checked against two independent real
 * scalar-NA references -- not re-derived here): reflection tracking (S11/S22) is the dB-domain
 * average of Open and Short; transmission tracking (S21/S12) is the Thru reading directly, no
 * averaging. Load is not used -- informational directivity-floor figure only, out of scope here.
 */
enum class CalStep(val label: String, val sParams: Set<SParam>) {
    OPEN_PORT1("Open @ port1", setOf(SParam.S11)),
    SHORT_PORT1("Short @ port1", setOf(SParam.S11)),
    OPEN_PORT2("Open @ port2", setOf(SParam.S22)),
    SHORT_PORT2("Short @ port2", setOf(SParam.S22)),
    THRU("Thru (both ports)", setOf(SParam.S21, SParam.S12)),
}

/**
 * Per-impedance-mode calibration capture: 5 steps (Open/Short @ each port, Thru), each a raw
 * (uncorrected) sweep capture; "Compute & Upload" derives and uploads the 4 correction curves
 * for this mode once all 5 are captured. Must share the same frequency grid (f0/f1/points) as
 * the other impedance mode and any previously-calibrated set on the device -- see
 * [CalibrationWizardState] for why and how that's enforced.
 */
class CalModeWizardState(
    val impedance: Impedance,
    private val client: SnaClient,
    private val scope: CoroutineScope,
) {
    private val _captures = MutableStateFlow<Map<CalStep, List<SweepPoint>>>(emptyMap())
    val captures: StateFlow<Map<CalStep, List<SweepPoint>>> = _captures.asStateFlow()

    private val _measuringStep = MutableStateFlow<CalStep?>(null)
    val measuringStep: StateFlow<CalStep?> = _measuringStep.asStateFlow()

    private val _started = MutableStateFlow(false)
    val started: StateFlow<Boolean> = _started.asStateFlow()

    private val _uploading = MutableStateFlow(false)
    val uploading: StateFlow<Boolean> = _uploading.asStateFlow()

    private val _uploaded = MutableStateFlow(false)
    val uploaded: StateFlow<Boolean> = _uploaded.asStateFlow()

    private val _verifyResult = MutableStateFlow<Boolean?>(null) // null = not run, else all points reported cal=true
    val verifyResult: StateFlow<Boolean?> = _verifyResult.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    val allCaptured: Boolean get() = CalStep.entries.all { it in _captures.value }

    private fun calSetFor(sParam: SParam): CalSet =
        CalSet.entries.first { it.wireName == "${sParam.wireName}_${impedance.ohms}" }

    fun reset() {
        _captures.value = emptyMap()
        _started.value = false
        _uploaded.value = false
        _verifyResult.value = null
        _error.value = null
    }

    /** Clears this mode's 4 sets on-device so sweep() returns raw (uncorrected) readings for a
     *  fresh capture -- necessary before re-measuring, otherwise sweep() would return values
     *  already corrected by whatever calibration is currently stored, silently poisoning the
     *  new capture. Does NOT reset the shared grid (only a full cal_clear("all") does that --
     *  see [CalibrationWizardState]). */
    fun beginOrRestart() {
        if (_measuringStep.value != null || !_uploading.compareAndSet(false, true)) return
        _started.value = false
        scope.launch {
            _error.value = null
            runCatching {
                SParam.entries.forEach { client.calClear(calSetFor(it)) }
            }.onFailure { _error.value = it.message; _uploading.value = false; return@launch }
            _captures.value = emptyMap()
            _uploaded.value = false
            _verifyResult.value = null
            _started.value = true
            _uploading.value = false
        }
    }

    fun measure(step: CalStep, f0Hz: Long, f1Hz: Long, points: Int) {
        if (!_started.value || _uploading.value || !_measuringStep.compareAndSet(null, step)) return
        scope.launch {
            _error.value = null
            runCatching {
                client.sweep(f0Hz, f1Hz, points, log = false, z = impedance,
                    params = step.sParams, allowUnlocked = false, attenuation = 0).toList().also { result ->
                    check(result.all { it.locked }) { "Source lock was not confirmed; repeat the calibration capture" }
                }
            }.onSuccess { result ->
                _captures.value = _captures.value + (step to result)
            }.onFailure { _error.value = it.message }
            _measuringStep.value = null
        }
    }

    fun computeAndUpload(f0Hz: Long, f1Hz: Long) {
        if (!allCaptured || _measuringStep.value != null || !_uploading.compareAndSet(false, true)) return
        scope.launch {
            _error.value = null
            runCatching {
                val open1 = _captures.value.getValue(CalStep.OPEN_PORT1)
                val short1 = _captures.value.getValue(CalStep.SHORT_PORT1)
                val open2 = _captures.value.getValue(CalStep.OPEN_PORT2)
                val short2 = _captures.value.getValue(CalStep.SHORT_PORT2)
                val thru = _captures.value.getValue(CalStep.THRU)

                val grid = open1.map { it.f }
                check(listOf(short1, open2, short2, thru).all { it.map { p -> p.f } == grid }) { "Calibration captures must share the same frequency grid" }
                check(grid.first() == f0Hz && grid.last() == f1Hz) { "Calibration range changed since capture" }
                val s11 = open1.indices.map { i -> (requireNotNull(open1[i].s11_db) + requireNotNull(short1[i].s11_db)) / 2.0 }
                val s22 = open2.indices.map { i -> (requireNotNull(open2[i].s22_db) + requireNotNull(short2[i].s22_db)) / 2.0 }
                val s21 = thru.map { requireNotNull(it.s21_db) }
                val s12 = thru.map { requireNotNull(it.s12_db) }

                client.calSet(calSetFor(SParam.S11), CalCurve(f0Hz, f1Hz, s11))
                client.calSet(calSetFor(SParam.S21), CalCurve(f0Hz, f1Hz, s21))
                client.calSet(calSetFor(SParam.S12), CalCurve(f0Hz, f1Hz, s12))
                client.calSet(calSetFor(SParam.S22), CalCurve(f0Hz, f1Hz, s22))
            }.onSuccess {
                _uploaded.value = true
            }.onFailure { e ->
                _error.value = if (e is SnaProtocolException && e.message == "grid_mismatch") {
                    "Grid mismatch: this mode's grid doesn't match the device's existing " +
                        "calibration grid. Use \"Clear ALL calibration\" above to start fresh with a new grid."
                } else e.message
            }
            _uploading.value = false
        }
    }

    /** Runs a normal calibrated sweep over this mode's own captured grid and checks every
     *  returned point reports cal=true -- a quick sanity check that the upload actually took. */
    fun verify(f0Hz: Long, f1Hz: Long, points: Int) {
        scope.launch {
            _error.value = null
            runCatching {
                client.sweep(f0Hz, f1Hz, points, log = false, z = impedance,
                    params = setOf(SParam.S11, SParam.S21, SParam.S12, SParam.S22),
                    allowUnlocked = false, attenuation = 0).toList()
            }.onSuccess { result ->
                _verifyResult.value = result.isNotEmpty() && result.all { it.cal && it.locked }
            }.onFailure { _error.value = it.message }
        }
    }
}

/**
 * Top-level calibration wizard state: owns the ONE shared frequency grid (f0/f1/points) both
 * impedance modes must use (device constraint -- all 8 calibration sets share a single grid),
 * and the two [CalModeWizardState] instances.
 *
 * Grid handling: clearing an individual calibration set only resets the device's grid if EVERY
 * one of the 8 sets ends up cleared (confirmed by reading `eeprom_cal_clear()` in the firmware
 * directly) -- otherwise the existing grid silently stays in effect for any later `cal_set`, and
 * a mismatched upload fails with `grid_mismatch`. So: on open, probe all 8 sets via `calGet()`;
 * the first one that succeeds gives the device's authoritative current grid, which gets locked
 * into the f0/f1/points fields. If nothing is calibrated anywhere yet, the fields stay editable.
 */
class CalibrationWizardState(private val client: SnaClient, scope: CoroutineScope) {
    private val wizardScope = scope

    private val _f0 = MutableStateFlow(FrequencyLimits.MIN_HZ.toString())
    val f0: StateFlow<String> = _f0.asStateFlow()

    private val _f1 = MutableStateFlow(FrequencyLimits.MAX_HZ.toString())
    val f1: StateFlow<String> = _f1.asStateFlow()

    private val _points = MutableStateFlow("201")
    val points: StateFlow<String> = _points.asStateFlow()

    private val _gridLocked = MutableStateFlow(false)
    val gridLocked: StateFlow<Boolean> = _gridLocked.asStateFlow()

    private val _detecting = MutableStateFlow(true)
    val detecting: StateFlow<Boolean> = _detecting.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    val mode50 = CalModeWizardState(Impedance.FIFTY, client, wizardScope)
    val mode75 = CalModeWizardState(Impedance.SEVENTY_FIVE, client, wizardScope)

    fun setF0(v: String) { if (!_gridLocked.value) _f0.value = v }
    fun setF1(v: String) { if (!_gridLocked.value) _f1.value = v }
    fun setPoints(v: String) { if (!_gridLocked.value) _points.value = v }

    fun detectExistingGrid() {
        wizardScope.launch {
            _detecting.value = true
            for (set in CalSet.entries) {
                val result = runCatching { client.calGet(set) }
                if (result.isFailure) {
                    _error.value = result.exceptionOrNull()?.message
                    _gridLocked.value = true // Unknown existing grid must not be treated as empty.
                    break
                }
                val curve = result.getOrNull()
                if (curve != null) {
                    _f0.value = curve.f0.toString()
                    _f1.value = curve.f1.toString()
                    _points.value = curve.points.size.toString()
                    _gridLocked.value = true
                    if (!FrequencyLimits.isValidRange(curve.f0, curve.f1)) {
                        _error.value = "Existing calibration extends below 50 MHz. It remains usable for supported sweeps; clear all calibration to capture a new 50 MHz–6 GHz grid."
                    }
                    break
                }
            }
            _detecting.value = false
        }
    }

    fun clearAllAndUnlock() {
        wizardScope.launch {
            runCatching { client.calClear(null) }.onSuccess {
                _error.value = null
                _f0.value = FrequencyLimits.MIN_HZ.toString()
                _f1.value = FrequencyLimits.MAX_HZ.toString()
                _gridLocked.value = false
                mode50.reset()
                mode75.reset()
            }.onFailure { _error.value = it.message }
        }
    }
}
