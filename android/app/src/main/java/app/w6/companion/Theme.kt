package app.w6.companion

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext

// Material 3 tonal palettes from a walnut-and-amber seed, matching the stand (brown PLA, amber accent on the screen).
// Used before Android 12 and when dynamic colour is off; on Android 12+ the colours come from the wallpaper.
private val WalnutLight = lightColorScheme(
    primary = Color(0xFF8B5000), onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFDCBE), onPrimaryContainer = Color(0xFF2C1600),
    secondary = Color(0xFF735943), onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFFFDCC2), onSecondaryContainer = Color(0xFF2A1707),
    tertiary = Color(0xFF5B6237), onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFE0E7B0), onTertiaryContainer = Color(0xFF191E00),
    error = Color(0xFFBA1A1A), onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6), onErrorContainer = Color(0xFF410002),
    background = Color(0xFFFFF8F4), onBackground = Color(0xFF221A14),
    surface = Color(0xFFFFF8F4), onSurface = Color(0xFF221A14),
    surfaceVariant = Color(0xFFF3DFD1), onSurfaceVariant = Color(0xFF51443A),
    outline = Color(0xFF84746A), outlineVariant = Color(0xFFD6C3B6),
    surfaceContainerLowest = Color(0xFFFFFFFF), surfaceContainerLow = Color(0xFFFFF1E8),
    surfaceContainer = Color(0xFFFAEBE1), surfaceContainerHigh = Color(0xFFF4E5DB),
    surfaceContainerHighest = Color(0xFFEEDFD6),
    inverseSurface = Color(0xFF382E28), inverseOnSurface = Color(0xFFFFEDE3), inversePrimary = Color(0xFFFFB870),
)

private val WalnutDark = darkColorScheme(
    primary = Color(0xFFFFB870), onPrimary = Color(0xFF4A2800),
    primaryContainer = Color(0xFF693C00), onPrimaryContainer = Color(0xFFFFDCBE),
    secondary = Color(0xFFE2C0A4), onSecondary = Color(0xFF412C19),
    secondaryContainer = Color(0xFF5A422D), onSecondaryContainer = Color(0xFFFFDCC2),
    tertiary = Color(0xFFC4CB96), onTertiary = Color(0xFF2E330D),
    tertiaryContainer = Color(0xFF444A22), onTertiaryContainer = Color(0xFFE0E7B0),
    error = Color(0xFFFFB4AB), onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A), onErrorContainer = Color(0xFFFFDAD6),
    background = Color(0xFF1A120C), onBackground = Color(0xFFF0DFD5),
    surface = Color(0xFF1A120C), onSurface = Color(0xFFF0DFD5),
    surfaceVariant = Color(0xFF51443A), onSurfaceVariant = Color(0xFFD6C3B6),
    outline = Color(0xFF9E8E82), outlineVariant = Color(0xFF51443A),
    surfaceContainerLowest = Color(0xFF140D07), surfaceContainerLow = Color(0xFF221A14),
    surfaceContainer = Color(0xFF271E17), surfaceContainerHigh = Color(0xFF322821),
    surfaceContainerHighest = Color(0xFF3D332B),
    inverseSurface = Color(0xFFF0DFD5), inverseOnSurface = Color(0xFF382E28), inversePrimary = Color(0xFF8B5000),
)

@Composable
fun W6Theme(dark: Boolean = isSystemInDarkTheme(), dynamic: Boolean = true, content: @Composable () -> Unit) {
    val scheme: ColorScheme = when {
        dynamic && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (dark) dynamicDarkColorScheme(LocalContext.current) else dynamicLightColorScheme(LocalContext.current)
        dark -> WalnutDark
        else -> WalnutLight
    }
    MaterialTheme(colorScheme = scheme, content = content)
}
