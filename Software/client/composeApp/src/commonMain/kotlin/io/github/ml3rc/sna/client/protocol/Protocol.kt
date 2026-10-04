package io.github.ml3rc.sna.client.protocol

import kotlinx.serialization.Serializable

/**
 * Data shapes and constants mirroring `Software/protocol/PROTOCOL.md`. Keep this file in sync
 * with that document -- it's the wire-format source of truth for both firmware and client.
 */

/** The 8 calibration sets (see `protocol/EEPROM_LAYOUT.md`) -- fixed storage order on-device. */
enum class CalSet(val wireName: String) {
    S11_50("s11_50"), S21_50("s21_50"), S12_50("s12_50"), S22_50("s22_50"),
    S11_75("s11_75"), S21_75("s21_75"), S12_75("s12_75"), S22_75("s22_75"),
}

enum class SParam(val wireName: String) {
    S11("s11"), S21("s21"), S12("s12"), S22("s22"),
}

enum class Impedance(val ohms: Int) {
    FIFTY(50), SEVENTY_FIVE(75),
}

enum class SdiCmp(val wireName: String) {
    LESS_THAN("lt"), GREATER_THAN("gt"),
}

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
)

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
    val state: String, // "idle" | "sweeping" | "error"
    val z: Int,
    val last_error: String? = null,
)

@Serializable
data class DeviceInfo(
    val fw: String,
    val hw_rev: Int,
    val proto: Int,
    val sweep_atten: Boolean = false,
)

class SnaProtocolException(message: String) : Exception(message)
