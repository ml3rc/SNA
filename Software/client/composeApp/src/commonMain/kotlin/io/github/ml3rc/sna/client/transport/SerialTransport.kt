package io.github.ml3rc.sna.client.transport

import kotlinx.coroutines.flow.Flow

/** One discoverable serial-capable device, platform-specific underneath. */
data class SerialPortInfo(
    val id: String,
    val displayName: String,
)

/**
 * Minimal line-oriented serial transport abstraction. Desktop backs this with jSerialComm
 * (a real OS virtual COM port), Android with usb-serial-for-android (talks CDC-ACM framing
 * directly over the USB Host API, no OS-level COM port involved) -- this interface hides that
 * split so [io.github.ml3rc.sna.client.client.SnaClient] and the UI don't need to know which
 * platform they're on.
 */
interface SerialTransport {
    /** Enumerate only devices matching the SNA USB vendor/product IDs. On Android these also
     *  need a matching serial driver; no permission prompt yet, that happens in [open]. */
    suspend fun listPorts(): List<SerialPortInfo>

    /** Open the given port for communication. Throws on failure (incl. a denied Android USB
     *  permission prompt, or the device having been unplugged since [listPorts]). */
    suspend fun open(port: SerialPortInfo)

    fun close()

    val isOpen: Boolean

    /** One line per incoming protocol message, newline stripped. The flow completes when the
     *  transport is closed or the underlying read fails. */
    fun incomingLines(): Flow<String>

    /** Writes one line, appending the protocol's `\n` terminator. */
    suspend fun writeLine(line: String)
}
