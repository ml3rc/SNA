package io.github.ml3rc.sna.client

import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.transport.DesktopSerialTransport
import io.github.ml3rc.sna.client.ui.App

fun main() = application {
    Window(onCloseRequest = ::exitApplication, title = "SNA · Scalar network analyzer",
        state = rememberWindowState(width = 1280.dp, height = 860.dp)) {
        App(createTransport = { DesktopSerialTransport() })
    }
}
