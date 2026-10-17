package io.github.ml3rc.sna.client.protocol

/** Argument limits from PROTOCOL.md, shared by the client's validation and its UI controls. */
object ProtocolLimits {
    /** `atten`: 0.25 dB steps below maximum output. */
    const val ATTEN_MIN = 0
    const val ATTEN_MAX = 127
    val ATTEN = ATTEN_MIN..ATTEN_MAX

    /** `sweep` `points`. */
    const val SWEEP_POINTS_MIN = 1
    const val SWEEP_POINTS_MAX = 4096
    val SWEEP_POINTS = SWEEP_POINTS_MIN..SWEEP_POINTS_MAX

    /** `cal_set` `points` (the stored calibration grid). */
    const val CAL_POINTS_MAX = 240
    val CAL_POINTS = 1..CAL_POINTS_MAX

    /** `sdi_test_set`: at most this many points. */
    const val SDI_POINTS_MAX = 15

    /** dB values stored as 0.01 dB `int16` (calibration points, SDI thresholds). */
    const val DB_MIN = -327.68
    const val DB_MAX = 327.67
    val DB = DB_MIN..DB_MAX
}
