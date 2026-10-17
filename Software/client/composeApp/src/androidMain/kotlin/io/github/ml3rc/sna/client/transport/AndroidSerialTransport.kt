package io.github.ml3rc.sna.client.transport

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbManager
import androidx.core.content.ContextCompat
import com.hoho.android.usbserial.driver.UsbSerialDriver
import com.hoho.android.usbserial.driver.UsbSerialPort
import com.hoho.android.usbserial.driver.UsbSerialProber
import com.hoho.android.usbserial.util.SerialInputOutputManager
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.channels.trySendBlocking
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.suspendCancellableCoroutine
import java.io.IOException
import kotlin.coroutines.resume

private const val ACTION_USB_PERMISSION = "io.github.ml3rc.sna.client.USB_PERMISSION"

/** Android serial transport, backed by usb-serial-for-android (mik3y) -- talks directly to the
 *  device's CDC-ACM interface over the USB Host API, no root, one runtime permission prompt. */
class AndroidSerialTransport(private val context: Context) : SerialTransport {
    private val manager = context.getSystemService(Context.USB_SERVICE) as UsbManager
    private var driver: UsbSerialDriver? = null
    private var serialPort: UsbSerialPort? = null
    private var ioManager: SerialInputOutputManager? = null

    override suspend fun listPorts(): List<SerialPortInfo> {
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(manager)
        return drivers.filter { SnaUsbIdentity.matches(it.device.vendorId, it.device.productId) }.map { d ->
            SerialPortInfo(
                id = d.device.deviceName,
                displayName = "${d.device.productName ?: d.device.deviceName} (VID ${d.device.vendorId}, PID ${d.device.productId})",
            )
        }
    }

    override suspend fun open(port: SerialPortInfo) {
        val drivers = UsbSerialProber.getDefaultProber().findAllDrivers(manager)
        val d = drivers.firstOrNull { it.device.deviceName == port.id }
            ?: throw IOException("Device ${port.id} no longer present")
        if (!SnaUsbIdentity.matches(d.device.vendorId, d.device.productId)) {
            throw IOException("Device ${port.id} is not an SNA")
        }
        driver = d

        if (!manager.hasPermission(d.device)) {
            requestPermission(d.device)
        }

        val connection = manager.openDevice(d.device)
            ?: throw IOException("Could not open ${port.id} — permission denied or device detached")
        val sp = d.ports[0]
        try {
            sp.open(connection)
            sp.setParameters(115200, 8, UsbSerialPort.STOPBITS_1, UsbSerialPort.PARITY_NONE)
            // TinyUSB CDC considers the host connected once DTR is asserted.
            sp.setDTR(true)
            serialPort = sp
        } catch (e: Exception) {
            runCatching { sp.close() }
            connection.close()
            throw e
        }
    }

    private suspend fun requestPermission(device: UsbDevice): Unit = suspendCancellableCoroutine { cont ->
        val pi = PendingIntent.getBroadcast(context, 0,
            Intent(ACTION_USB_PERMISSION).setPackage(context.packageName), PendingIntent.FLAG_IMMUTABLE)
        val receiver = object : BroadcastReceiver() {
            override fun onReceive(ctx: Context?, intent: Intent?) {
                runCatching { context.unregisterReceiver(this) }
                if (!cont.isActive) return
                if (intent?.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false) == true) {
                    cont.resume(Unit)
                } else {
                    cont.cancel(IOException("USB permission denied"))
                }
            }
        }
        ContextCompat.registerReceiver(context, receiver, IntentFilter(ACTION_USB_PERMISSION),
            ContextCompat.RECEIVER_NOT_EXPORTED)
        cont.invokeOnCancellation { runCatching { context.unregisterReceiver(receiver) } }
        if (cont.isActive) {
            runCatching { manager.requestPermission(device, pi) }.onFailure {
                runCatching { context.unregisterReceiver(receiver) }
                cont.cancel(it)
            }
        }
    }

    override fun close() {
        ioManager?.stop()
        ioManager = null
        runCatching { serialPort?.close() }
        serialPort = null
        driver = null
    }

    override val isOpen: Boolean
        get() = serialPort != null

    override fun incomingLines(): Flow<String> = callbackFlow {
        val sp = serialPort
        if (sp == null) {
            close()
            return@callbackFlow
        }
        // Raw bytes, not text: a multi-byte UTF-8 character can be split across two USB chunks,
        // so only complete lines (split on 0x0A, which never occurs inside a UTF-8 sequence) are decoded.
        val buffer = java.io.ByteArrayOutputStream()
        val listener = object : SerialInputOutputManager.Listener {
            override fun onNewData(data: ByteArray) {
                var start = 0
                for (i in data.indices) {
                    if (data[i] != '\n'.code.toByte()) continue
                    buffer.write(data, start, i - start)
                    start = i + 1
                    val line = buffer.toString(Charsets.UTF_8.name())
                    buffer.reset()
                    if (trySendBlocking(line.trimEnd('\r')).isFailure) return
                }
                buffer.write(data, start, data.size - start)
            }

            override fun onRunError(e: Exception) {
                close(e)
            }
        }
        val manager = SerialInputOutputManager(sp, listener)
        ioManager = manager
        manager.start()
        awaitClose { manager.stop() }
    }

    override suspend fun writeLine(line: String) {
        val sp = serialPort ?: throw IOException("Port not open")
        sp.write((line + "\n").toByteArray(Charsets.UTF_8), 1000)
    }
}
