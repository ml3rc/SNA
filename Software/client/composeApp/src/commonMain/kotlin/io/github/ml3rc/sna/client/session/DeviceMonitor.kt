package io.github.ml3rc.sna.client.session

import io.github.ml3rc.sna.client.client.SnaClient
import io.github.ml3rc.sna.client.protocol.DeviceStatus
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/**
 * Polls `status` for as long as the connection lives, whatever tab is open. `status` is answered
 * by the device even during a sweep, so this is also how the client notices things it did not
 * start itself (on-board button, a mode entered from the console).
 */
class DeviceMonitor(
    private val client: SnaClient,
    private val scope: CoroutineScope,
    private val intervalMs: Long = 1000,
) {
    private val _status = MutableStateFlow<DeviceStatus?>(null)
    val status: StateFlow<DeviceStatus?> = _status.asStateFlow()

    private val refreshes = Mutex()

    fun start() {
        scope.launch {
            while (true) {
                refresh()
                delay(intervalMs)
            }
        }
    }

    /**
     * Reads `status` now. Refreshes run one at a time, so a reply that was requested before a
     * change (a sweep ending, a mode released) can never overwrite the answer of one requested after it.
     */
    suspend fun refresh() = refreshes.withLock {
        try {
            _status.value = client.status()
        } catch (e: CancellationException) {
            throw e
        } catch (e: Exception) {
            // A dead link is reported by the client's own failure flow; keep the last status.
        }
    }

    fun refreshSoon() {
        scope.launch { refresh() }
    }
}
