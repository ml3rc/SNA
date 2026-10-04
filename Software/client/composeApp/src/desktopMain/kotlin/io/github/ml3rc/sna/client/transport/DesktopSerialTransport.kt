package io.github.ml3rc.sna.client.transport

import com.fazecast.jSerialComm.SerialPort
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.channels.trySendBlocking
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.withContext
import java.io.BufferedReader
import java.io.IOException
import java.io.InputStreamReader
import java.io.OutputStream

/** Desktop (Windows/Linux/macOS) serial transport, backed by jSerialComm -- talks to the
 *  device's CDC-ACM virtual COM port with no driver install needed on any of the three OSes. */
class DesktopSerialTransport : SerialTransport {
    private var port: SerialPort? = null
    private var output: OutputStream? = null

    override suspend fun listPorts(): List<SerialPortInfo> = withContext(Dispatchers.IO) {
        // Match USB IDs, since Windows' generic CDC names do not identify the hardware.
        SerialPort.getCommPorts().filter { SnaUsbIdentity.matches(it.vendorID, it.productID) }.map {
            val vidPid = "%04x:%04x".format(it.vendorID, it.productID)
            SerialPortInfo(id = it.systemPortName, displayName = "${it.systemPortName} — ${it.descriptivePortName} (${vidPid})")
        }
    }

    override suspend fun open(port: SerialPortInfo) = withContext(Dispatchers.IO) {
        // Re-enumerate so a stale selection cannot open another device reusing the port name.
        val sp = SerialPort.getCommPorts().firstOrNull { it.systemPortName == port.id }
            ?: throw IOException("Device ${port.id} no longer present")
        if (!SnaUsbIdentity.matches(sp.vendorID, sp.productID)) {
            throw IOException("Device ${port.id} is not an SNA")
        }
        sp.setComPortParameters(115200, 8, SerialPort.ONE_STOP_BIT, SerialPort.NO_PARITY)
        // TIMEOUT_READ_BLOCKING (the mode this used to use) blocks a read() call until the
        // ENTIRE requested buffer is filled, not until any data arrives -- BufferedReader
        // defaults to an 8192-byte internal buffer, so readLine() would block forever for any
        // single short JSON response line (ours are ~70-150 bytes), no matter how many valid
        // responses the device actually sent. Confirmed as the real root cause of this client's
        // connection timeouts on real hardware, 2026-10-02, with a minimal reproduction against
        // jSerialComm directly: identical setup only ever differing in this one timeout mode
        // hung indefinitely with TIMEOUT_READ_BLOCKING and responded in single-digit ms with
        // TIMEOUT_READ_SEMI_BLOCKING (blocks until AT LEAST ONE byte is available, or the
        // timeout expires) -- the correct mode for a line-oriented protocol with variable,
        // typically-small message sizes.
        sp.setComPortTimeouts(SerialPort.TIMEOUT_READ_SEMI_BLOCKING, 0, 0)
        if (!sp.openPort()) throw IOException("Failed to open ${port.id}")
        // USB CDC-ACM devices (this board's TinyUSB-based firmware included) gate whether
        // they're "connected" on the host asserting DTR -- the firmware's tud_cdc_connected()
        // reflects the CDC class's SET_CONTROL_LINE_STATE request, and BOTH its read loop
        // (main.c) and every response write (protocol.c's send_json()) are conditioned on it.
        // Without asserting DTR here, the device never processes an incoming command and never
        // sends a reply, which just looks like a timeout with no further clue. Confirmed as the
        // root cause, 2026-10-02: a pyserial-based test (which asserts DTR by default on open())
        // got instant responses against the exact same port/firmware this client was timing out
        // on, repeatedly, independent of running as admin -- jSerialComm does not assert DTR on
        // openPort() the way pyserial does, so it has to be done explicitly.
        sp.setDTR()
        this@DesktopSerialTransport.port = sp
        this@DesktopSerialTransport.output = sp.outputStream
    }

    override fun close() {
        port?.closePort()
        port = null
        output = null
    }

    override val isOpen: Boolean
        get() = port?.isOpen == true

    override fun incomingLines(): Flow<String> = callbackFlow {
        val sp = port
        if (sp == null) {
            close()
            return@callbackFlow
        }
        val reader = BufferedReader(InputStreamReader(sp.inputStream))
        val thread = Thread {
            try {
                while (sp.isOpen) {
                    val line = reader.readLine() ?: break
                    if (trySendBlocking(line).isFailure) break
                }
            } catch (e: IOException) {
                close(e)
            }
            close()
        }
        thread.isDaemon = true
        thread.start()
        awaitClose { thread.interrupt() }
    }

    override suspend fun writeLine(line: String) = withContext(Dispatchers.IO) {
        val out = output ?: throw IOException("Port not open")
        out.write((line + "\n").toByteArray(Charsets.UTF_8))
        out.flush()
    }
}
