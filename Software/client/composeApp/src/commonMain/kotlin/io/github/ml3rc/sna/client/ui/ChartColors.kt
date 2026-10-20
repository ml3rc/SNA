package io.github.ml3rc.sna.client.ui

import androidx.compose.ui.graphics.Color

/** Shared per-S-parameter trace colors, consistent across the Sweep and Calibration screens'
 *  charts, chosen to read clearly against the app's dark "bench instrument" theme
 *  (see ui/theme/Theme.kt) alongside its copper/violet accents. */
object ChartColors {
    /** "A tool is running" marker on menu buttons. */
    val Running = Color(0xFF4CAF50)

    // Classic network-analyzer trace order (Tr1..Tr4 = S11, S21, S12, S22: yellow, cyan,
    // magenta, green), each pulled slightly off the pure primary so it doesn't glare.
    val S11 = Color(0xFFF2E450) // yellow
    val S21 = Color(0xFF4FD6EA) // cyan
    val S12 = Color(0xFFE466D8) // magenta
    val S22 = Color(0xFF5ED86A) // green

    // Calibration wizard: up to 6 traces at once (Open/Short at each port, Thru's two
    // directions) -- grouped by which final S-param they feed into (S11 yellow family, S22 green
    // family), with a darker shade distinguishing Open from Short within each pair.
    val CalOpen1 = S11
    val CalShort1 = Color(0xFF9A8F2C)
    val CalOpen2 = S22
    val CalShort2 = Color(0xFF3A8A44)
    val CalThruS21 = S21
    val CalThruS12 = S12
}
