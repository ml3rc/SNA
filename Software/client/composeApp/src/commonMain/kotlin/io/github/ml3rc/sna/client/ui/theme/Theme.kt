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
    primary = Color(0xFF9CBCEE), onPrimary = Color(0xFF142238),
    primaryContainer = Color(0xFF263345), onPrimaryContainer = Color(0xFFD2E1F8),
    secondary = Color(0xFFF2BE74), onSecondary = Color(0xFF30200C),
    secondaryContainer = Color(0xFF47351F), onSecondaryContainer = Color(0xFFFFDFB0),
    background = Color(0xFF151719), onBackground = Color(0xFFE9EAEC),
    surface = Color(0xFF1C1F22), onSurface = Color(0xFFE9EAEC),
    surfaceVariant = Color(0xFF282C31), onSurfaceVariant = Color(0xFFB1B5BC),
    outline = Color(0xFF565C65), outlineVariant = Color(0xFF353A40),
    error = Color(0xFFFF8990), onError = Color(0xFF3D1016),
    errorContainer = Color(0xFF481D25), onErrorContainer = Color(0xFFFFD9DD),
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