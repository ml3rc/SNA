package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.TempReading
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

/**
 * Synthesizer temperature readback, polled while the Diagnostics tab is open ([poll] runs in the
 * screen's effect). The device answers during sweeps and generator output too, from its last live
 * value (flagged `cached`) instead of touching the RF hardware.
 */
class HousekeepingState(private val client: SnaClient) {
    private val _temp = MutableStateFlow<TempReading?>(null)
    val temp: StateFlow<TempReading?> = _temp.asStateFlow()

    private val _tempFetched = MutableStateFlow(false)
    val tempFetched: StateFlow<Boolean> = _tempFetched.asStateFlow()

    private val _error = MutableStateFlow<String?>(null)
    val error: StateFlow<String?> = _error.asStateFlow()

    suspend fun poll(intervalMs: Long = 1000) {
        while (true) {
            try {
                _temp.value = client.srcTempRead()
                _tempFetched.value = true
                _error.value = null
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                _error.value = e.message
            }
            delay(intervalMs)
        }
    }
}
