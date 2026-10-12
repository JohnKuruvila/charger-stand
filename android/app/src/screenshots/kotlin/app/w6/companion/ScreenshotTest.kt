package app.w6.companion

import android.graphics.Bitmap
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onRoot
import com.github.takahirom.roborazzi.captureRoboImage
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File

/**
 * Renders the screens to PNGs to look at. Opt-in only: scripts/screenshots.sh, never CI or
 * `./gradlew test`. Output: app/build/screenshots. The data is made up; the stand picture is the firmware's desktop
 * preview render when it has been built (firmware/w6/preview).
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [36], qualifiers = "w412dp-h915dp-xxhdpi")
class ScreenshotTest {
    @get:Rule val compose = createComposeRule()
    private val dir = System.getProperty("screenshots.dir") ?: "build/screenshots"

    private fun shot(name: String, dark: Boolean = false, content: @Composable () -> Unit) {
        compose.setContent {
            W6Theme(dark = dark, dynamic = false) {
                Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) { content() }
            }
        }
        compose.waitForIdle()
        compose.onRoot().captureRoboImage("$dir/$name.png")
    }

    /** A 24-bit BMP from the firmware's desktop preview, or null if it hasn't been rendered. */
    private fun standPicture(name: String): ImageBitmap? {
        val f = File("../../firmware/w6/preview/$name.bmp")
        if (!f.exists()) return null
        val b = f.readBytes()
        fun u32(i: Int) = (b[i].toInt() and 0xFF) or ((b[i + 1].toInt() and 0xFF) shl 8) or
            ((b[i + 2].toInt() and 0xFF) shl 16) or ((b[i + 3].toInt() and 0xFF) shl 24)
        val off = u32(10)
        val px = IntArray(240 * 240) { i ->
            val p = off + i * 3
            (0xFF shl 24) or ((b[p + 2].toInt() and 0xFF) shl 16) or ((b[p + 1].toInt() and 0xFF) shl 8) or (b[p].toInt() and 0xFF)
        }
        return Bitmap.createBitmap(px, 240, 240, Bitmap.Config.ARGB_8888).asImageBitmap()
    }

    private val status = StandStatus(
        version = "0.6.5", uptimeS = 7300, resetReason = "restart", rssiDbm = -41, freeHeap = 76000, rotation = 1,
        face = "stack", chipTempC = 52.7,
        features = setOf("phone", "timer", "faces", "rotation", "log", "media", "clock_24h", "screenshot_rle", "utc_offset",
            "charging_styles"),
        timerLeftS = null, clock24h = false, utcOffsetS = 19800,
        faces = listOf("classic", "analog", "stack", "stack_calm", "orbit", "orbit_night", "segment", "segment_teal",
            "words", "walnut", "minimal", "terminal", "terminal_amber", "wormhole", "aurora", "galaxy", "silk", "sky",
            "walnut_grain"),
        chargingStyle = "warp", chargingStyles = listOf("classic", "warp", "swarm"),
    )

    private fun ui(listener: Boolean = true, timer: Long? = null, picture: String = "14_media_playing") = AppViewModel.Ui(
        paired = true, onboarded = true, listenerAllowed = listener, status = status.copy(timerLeftS = timer),
        screen = standPicture(picture), reachable = true, host = "192.168.0.50",
        shares = Prefs.Share.entries.associateWith { it.default }, timerLeftS = timer,
    )

    private val link = Link.State(
        running = true, background = true, onWifi = true, phone = Logic.PhoneReport(72, true, 5.1),
        track = "Bohemian Rhapsody · Queen", playing = true,
    )

    @Composable private fun home(u: AppViewModel.Ui, l: Link.State = link) =
        HomeScreen(u, l, SnackbarHostState(), onSettings = {}, onShare = { _, _ -> }, onFaces = {}, onFace = {},
            onChargingStyle = {}, onTimer = { _, _ -> }, onCancelTimer = {}, onRefresh = {}, onRetry = {})

    @Test fun homeLight() = shot("01-home-light") { home(ui()) }
    @Test fun homeDark() = shot("02-home-dark", dark = true) { home(ui(picture = "30_face_wormhole")) }
    @Test fun homeNoAccessTimer() = shot("03-home-no-access-timer") {
        home(ui(listener = false, timer = 1499, picture = "10_timer_start"), link.copy(track = null, phone = null))
    }
    @Test fun faces() = shot("04b-faces") { FacesScreen(ui(), SnackbarHostState(), {}, {}) }
    @Test fun facesDark() = shot("04c-faces-dark", dark = true) {
        FacesScreen(ui().copy(status = status.copy(face = "words")), SnackbarHostState(), {}, {})
    }
    @Test fun homeOffline() = shot("04-home-offline") {
        home(ui().copy(reachable = false, screen = null, status = null))
    }

    private val actions = PairActions({ AppViewModel.PairStart.Showing("192.168.0.50", 120) }, { _, _, _ -> null }, { _, _, _ -> null })
    @Test fun pair() = shot("05-pair") { PairScreen(listOf(Discovery.Found("w6", "192.168.0.50")), actions) }
    @Test fun pairSearching() = shot("06-pair-searching", dark = true) { PairScreen(emptyList(), actions) }
    @Test fun pairCode() = shot("05b-pair-code") { PairScreen(emptyList(), actions, previewStep = "code") }
    @Test fun pairCodeDark() = shot("05c-pair-code-dark", dark = true) { PairScreen(emptyList(), actions, previewStep = "code") }
    @Test fun pairToken() = shot("05d-pair-token") { PairScreen(emptyList(), actions, previewStep = "token") }
    @Test fun pairLink() = shot("05e-pair-link") {
        PairScreen(emptyList(), actions, incoming = Logic.PairLink("192.168.0.50", "k7m2p9xq"))
    }
    @Test fun access() = shot("07-access") { AccessScreen(false) {} }

    @Test fun settings() = shot("08-settings") {
        SettingsScreen(ui(), SnackbarHostState(), "0.1.0", {}, {}, {}, {}, {}, {}, {}, {})
    }

    @Test fun log() = shot("09-log") {
        LogScreen(
            StandLog(
                listOf(
                    StandLog.Boot(8, 1791725099, "restart", 47, true, true, true, false, 0, 71, 90000, null),
                    StandLog.Boot(7, null, "power on", 602, true, false, false, true, 0, 74, 80000, "setup idle"),
                    StandLog.Boot(6, null, "power on", 0, false, false, false, false, 0, null, 100000, null),
                    StandLog.Boot(5, null, "power on", 0, false, false, false, false, 0, null, 100000, null),
                    StandLog.Boot(3, 1791724989, "restart", 60, false, false, true, false, 0, 58, 98000, null),
                    StandLog.Boot(2, 1791724928, "restart", 61, true, false, true, false, 1, 62, 98000, "update"),
                ),
                emptyList(), 0,
            ),
            onBack = {}, onLoad = {},
        )
    }

    @Test fun firmware() = shot("10-firmware") {
        FirmwareScreen(AppViewModel.Ota(name = "firmware.bin", size = 1_327_272, valid = true, progress = 0.42f),
            "0.3.0", {}, {}, {}, {})
    }
}
