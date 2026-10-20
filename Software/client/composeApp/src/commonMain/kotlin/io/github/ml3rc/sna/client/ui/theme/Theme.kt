package io.github.ml3rc.sna.client.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

val SnaDarkColorScheme = darkColorScheme(
    // Copper primary + dusty-violet secondary on warm near-black: kept clear of the four
    // trace hues (yellow/cyan/magenta/green, see ChartColors) so UI accents never read as data.
    primary = Color(0xFFE39A6E), onPrimary = Color(0xFF3A1D0D),
    primaryContainer = Color(0xFF4A2E1F), onPrimaryContainer = Color(0xFFFFDCC8),
    secondary = Color(0xFFB7A7DC), onSecondary = Color(0xFF2A2140),
    secondaryContainer = Color(0xFF3B3352), onSecondaryContainer = Color(0xFFE6DDFF),
    background = Color(0xFF0F0E0D), onBackground = Color(0xFFECE7E1),
    surface = Color(0xFF161412), onSurface = Color(0xFFECE7E1),
    surfaceVariant = Color(0xFF24211E), onSurfaceVariant = Color(0xFFB8B0A6),
    surfaceContainerLowest = Color(0xFF0B0A09), surfaceContainerLow = Color(0xFF141210),
    surfaceContainer = Color(0xFF1A1816), surfaceContainerHigh = Color(0xFF211E1B),
    surfaceContainerHighest = Color(0xFF292522),
    outline = Color(0xFF5F5850), outlineVariant = Color(0xFF34302B),
    error = Color(0xFFFF8A80), onError = Color(0xFF3D1210),
    errorContainer = Color(0xFF4A1E1A), onErrorContainer = Color(0xFFFFDAD5),
)

private fun type(size: Int, height: Int, weight: FontWeight = FontWeight.Normal,
    mono: Boolean = false, tracking: Float = 0f) = TextStyle(
    fontFamily = if (mono) FontFamily.Monospace else FontFamily.SansSerif,
    fontSize = size.sp, lineHeight = height.sp, fontWeight = weight, letterSpacing = tracking.sp)

val SnaTypography = Typography(
    displayLarge = type(56, 62, FontWeight.Light),
    displayMedium = type(44, 50, FontWeight.Light), displaySmall = type(36, 42, FontWeight.Light),
    headlineLarge = type(30, 36, FontWeight.Medium), headlineMedium = type(26, 32, FontWeight.Medium),
    headlineSmall = type(22, 28, FontWeight.Medium),
    titleLarge = type(19, 26, FontWeight.Medium), titleMedium = type(16, 22, FontWeight.Medium),
    titleSmall = type(14, 20, FontWeight.Medium),
    bodyLarge = type(15, 22), bodyMedium = type(14, 20), bodySmall = type(12, 18),
    labelLarge = type(13, 18, FontWeight.SemiBold, tracking = .3f),
    labelMedium = type(11, 16, FontWeight.Medium, tracking = .1f),
    labelSmall = type(10, 15, tracking = .1f),
)

val SnaCompactTypography = SnaTypography.copy(
    displayMedium = type(34, 40, FontWeight.Light), headlineLarge = type(24, 30, FontWeight.Medium),
    headlineSmall = type(18, 24, FontWeight.Medium),
    bodyLarge = type(14, 20), bodyMedium = type(13, 19), labelLarge = type(12, 18, FontWeight.SemiBold),
)

val SnaShapes = Shapes(
    extraSmall = RoundedCornerShape(3.dp), small = RoundedCornerShape(3.dp),
    medium = RoundedCornerShape(3.dp), large = RoundedCornerShape(3.dp),
    extraLarge = RoundedCornerShape(3.dp),
)