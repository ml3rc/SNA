package io.github.ml3rc.sna.client.ui

import androidx.compose.ui.graphics.Color

/** Shared per-S-parameter trace colors, consistent across the Sweep and Calibration screens'
 *  charts, chosen to read clearly against the app's dark "bench instrument" theme
 *  (see ui/theme/Theme.kt) alongside its primary/secondary accents. */
object ChartColors {
    /** "A tool is running" marker on menu buttons. */
    val Running = Color(0xFF4CAF50)

    val S11 = Color(0xFF39E6C9) // primary cyan
    val S21 = Color(0xFFFFB74D) // secondary amber
    val S12 = Color(0xFFCE93D8) // soft magenta
    val S22 = Color(0xFF81C784) // soft green

    // Calibration wizard: up to 6 traces at once (Open/Short at each port, Thru's two
    // directions) -- grouped by which final S-param they feed into (S11 cyan family, S22 green
    // family), with a darker shade distinguishing Open from Short within each pair.
    val CalOpen1 = S11
    val CalShort1 = Color(0xFF1C8C7A)
    val CalOpen2 = S22
    val CalShort2 = Color(0xFF4C8C50)
    val CalThruS21 = S21
    val CalThruS12 = S12
}
