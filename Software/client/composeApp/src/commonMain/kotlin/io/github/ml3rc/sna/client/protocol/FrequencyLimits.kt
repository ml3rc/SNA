package io.github.ml3rc.sna.client.protocol

object FrequencyLimits {
    const val MIN_HZ = 50_000_000L
    const val MAX_HZ = 6_000_000_000L

    fun isValidRange(f0: Long, f1: Long) =
        f0 in MIN_HZ..MAX_HZ && f1 in f0..MAX_HZ

    fun requireRange(f0: Long, f1: Long) {
        require(isValidRange(f0, f1)) { "Frequency range must be within 50 MHz–6 GHz, with start ≤ stop" }
    }
}
