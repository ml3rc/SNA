package io.github.ml3rc.sna.client.protocol

import kotlinx.serialization.Serializable
import kotlinx.serialization.Transient

/**
 * Data shapes and constants mirroring `Software/protocol/PROTOCOL.md`. Keep this file in sync
 * with that document -- it's the wire-format source of truth for both firmware and client.
 */

/** The 8 calibration sets (see `protocol/EEPROM_LAYOUT.md`) -- fixed storage order on-device. */
enum class CalSet(val wireName: String, val sParam: SParam, val z: Impedance) {
    S11_50("s11_50", SParam.S11, Impedance.FIFTY), S21_50("s21_50", SParam.S21, Impedance.FIFTY),
    S12_50("s12_50", SParam.S12, Impedance.FIFTY), S22_50("s22_50", SParam.S22, Impedance.FIFTY),
    S11_75("s11_75", SParam.S11, Impedance.SEVENTY_FIVE), S21_75("s21_75", SParam.S21, Impedance.SEVENTY_FIVE),
    S12_75("s12_75", SParam.S12, Impedance.SEVENTY_FIVE), S22_75("s22_75", SParam.S22, Impedance.SEVENTY_FIVE);

    companion object {
        /** The set holding the correction for [sParam] at [z]; every combination exists. */
        fun of(sParam: SParam, z: Impedance): CalSet = entries.first { it.sParam == sParam && it.z == z }
    }
}

enum class SParam(val wireName: String) {
    S11("s11"), S21("s21"), S12("s12"), S22("s22");

    companion object {
        fun fromWire(name: String): SParam? = entries.firstOrNull { it.wireName == name }
    }
}

/** The four coupler states the detector can be switched to; a raw detector sweep reads these instead of S-parameters. */
enum class DetectorPath(val wireName: String, val label: String) {
    P1_FWD("p1_fwd", "Port 1 fwd"), P1_BWD("p1_bwd", "Port 1 bwd"),
    P2_FWD("p2_fwd", "Port 2 fwd"), P2_BWD("p2_bwd", "Port 2 bwd"),
}

enum class Impedance(val ohms: Int) {
    FIFTY(50), SEVENTY_FIVE(75);

    companion object {
        fun fromOhms(ohms: Int): Impedance? = entries.firstOrNull { it.ohms == ohms }
    }
}

enum class SdiCmp(val wireName: String) {
    LESS_THAN("lt"), GREATER_THAN("gt");

    companion object {
        fun fromWire(name: String): SdiCmp? = entries.firstOrNull { it.wireName == name }
    }
}

/** Calibration curves are captured (and the front-panel SDI test runs) at this attenuation: maximum output. */
const val CAL_CAPTURE_ATTEN = 0

@Serializable
data class SweepPoint(
    val pt: Int,
    val f: Long,
    val s11_db: Double? = null,
    val s21_db: Double? = null,
    val s12_db: Double? = null,
    val s22_db: Double? = null,
    val cal: Boolean = true,
    val locked: Boolean = true,
    // Raw detector sweep only: uncalibrated ADC code and detector-referred dBm per coupler path.
    val p1_fwd_raw: Int? = null,
    val p1_fwd_dbm: Double? = null,
    val p1_bwd_raw: Int? = null,
    val p1_bwd_dbm: Double? = null,
    val p2_fwd_raw: Int? = null,
    val p2_fwd_dbm: Double? = null,
    val p2_bwd_raw: Int? = null,
    val p2_bwd_dbm: Double? = null,
    /**
     * False for a client-side placeholder (a grid point the current pass has not reached, or the
     * tail of an interrupted one): it has no readings and must not be exported or counted as a
     * measurement. Never on the wire; every point the device sends is measured.
     */
    @Transient val measured: Boolean = true,
) {
    companion object {
        /** A grid point without readings yet: not measured, so neither locked nor calibrated. */
        fun placeholder(pt: Int, f: Long) = SweepPoint(pt = pt, f = f, cal = false, locked = false, measured = false)
    }

    fun detectorRaw(path: DetectorPath): Int? = when (path) {
        DetectorPath.P1_FWD -> p1_fwd_raw; DetectorPath.P1_BWD -> p1_bwd_raw
        DetectorPath.P2_FWD -> p2_fwd_raw; DetectorPath.P2_BWD -> p2_bwd_raw
    }

    fun detectorDbm(path: DetectorPath): Double? = when (path) {
        DetectorPath.P1_FWD -> p1_fwd_dbm; DetectorPath.P1_BWD -> p1_bwd_dbm
        DetectorPath.P2_FWD -> p2_fwd_dbm; DetectorPath.P2_BWD -> p2_bwd_dbm
    }
}

@Serializable
data class CalCurve(
    val f0: Long,
    val f1: Long,
    val points: List<Double>,
)

@Serializable
data class SdiTestPointConfig(
    val f: Long,
    val thresh_db: Double,
    val s_param: String, // "s11" | "s21" | "s12" | "s22" -- see SParam
    val cmp: String, // "lt" | "gt" -- see SdiCmp
)

@Serializable
data class SdiTestPointResult(
    val f: Long,
    val s_param: String, // "s11" | "s21" | "s12" | "s22" -- see SParam
    val val_db: Double,
    val pass: Boolean,
)

@Serializable
data class SdiTestResultEvent(
    val pass: Boolean,
    val results: List<SdiTestPointResult>,
)

@Serializable
data class DeviceStatus(
    /** The current mode: "idle" | "sweep" | "generator" | "power_meter" | "manual" -- see [DeviceMode]. */
    val mode: String = "idle",
    /** A sweep or the front-panel SDI test is running (most other commands get `busy`). */
    val sweeping: Boolean = false,
    val z: Int,
    /** The selected source port (1 or 2), also while the output is terminated. */
    val port: Int = 1,
    val atten: Int = 127,
    val gen_on: Boolean = false,
    val gen_f: Long? = null,
    /** The continuous power meter (`power_start`) is running, on [pwr_port]. */
    val pwr_on: Boolean = false,
    val pwr_port: Int? = null,
    /** Boot self-test failures, empty when the device booted fine: "eeprom" | "source_lock" | "detector". */
    val init_errors: List<String> = emptyList(),
)

/** The resulting settings echoed by `set`. */
@Serializable
data class DeviceSettings(val z: Int, val atten: Int)

/** One `power_read` result or `power` event. [dbm] is approximate and uncalibrated (detector-referenced), see PROTOCOL.md. */
@Serializable
data class PowerReading(
    val port: Int,
    val z: Int,
    val raw: Int,
    val volts: Double,
    val dbm: Double,
    /** Where [dbm] sits relative to the detector's usable range: "ok" | "low" | "high". */
    val range: String = "ok",
)

/** One coupler path read by `probe_read`. [min]/[max] are the extremes of the 16 samples behind the median [raw]. */
@Serializable
data class ProbeReading(
    val path: Int,
    val name: String,
    val raw: Int,
    val min: Int,
    val max: Int,
    val volts: Double,
    val dbm: Double,
    val range: String = "ok",
)

/** The S-parameter pair the source port measures, from the probe readings: raw ratio and with stored calibration applied. */
@Serializable
data class ProbeDerived(
    val refl: String,
    val refl_db: Double,
    val refl_cal_db: Double,
    val trans: String,
    val trans_db: Double,
    val trans_cal_db: Double,
    val cal: Boolean,
)

@Serializable
data class ProbeResult(
    val gen_on: Boolean = false,
    val port: Int,
    val z: Int,
    val atten: Int,
    val f: Long? = null,
    val readings: List<ProbeReading>,
    val derived: ProbeDerived? = null,
)

/** Synthesizer temperature. [cached] means the device answered with its last live reading because RF was busy. */
data class TempReading(val tempC: Double, val cached: Boolean, val ageMs: Long)

@Serializable
data class DeviceInfo(
    val fw: String,
    val hw_rev: Int,
    val proto: Int,
    val freq_min_hz: Long = FrequencyLimits.MIN_HZ,
    val freq_max_hz: Long = FrequencyLimits.MAX_HZ,
)

/**
 * A device error reply (or a client-side protocol failure). [code] is the device's `error` code
 * (`busy`, `bad_args`, ...), [field] the argument `bad_args` names. The message is the code, plus
 * the field for `bad_args`.
 */
class SnaProtocolException(message: String, val code: String? = null, val field: String? = null) : Exception(message)

/** Protocol version this client speaks; firmware speaking any other version is refused on connect. */
const val REQUIRED_PROTOCOL = 3

/**
 * The device's RF modes: one at a time, entered from idle with `mode_enter` and left
 * with `mode_exit`. Each RF command only works inside its mode -- see PROTOCOL.md "Modes".
 */
enum class DeviceMode(val wireName: String, val label: String) {
    IDLE("idle", "idle"),
    SWEEP("sweep", "sweep"),
    GENERATOR("generator", "generator"),
    POWER_METER("power_meter", "power meter"),
    MANUAL("manual", "manual");

    companion object {
        fun fromWire(name: String?): DeviceMode? = entries.firstOrNull { it.wireName == name }
    }
}
