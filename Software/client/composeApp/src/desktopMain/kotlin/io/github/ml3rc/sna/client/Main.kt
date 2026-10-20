package io.github.ml3rc.sna.client

import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.res.loadImageBitmap
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.transport.DesktopSerialTransport
import io.github.ml3rc.sna.client.ui.App

// Title-bar/taskbar icon; the installer/exe icon comes from icons/sna.ico via nativeDistributions.
private val appIcon = BitmapPainter(
    Thread.currentThread().contextClassLoader.getResourceAsStream("sna_icon.png")!!.use(::loadImageBitmap))

fun main() = application {
    Window(onCloseRequest = ::exitApplication, title = "SNA · Scalar network analyzer", icon = appIcon,
        state = rememberWindowState(width = 1280.dp, height = 860.dp)) {
        App(createTransport = { DesktopSerialTransport() })
    }
}
