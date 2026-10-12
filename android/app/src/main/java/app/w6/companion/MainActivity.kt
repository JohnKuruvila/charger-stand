package app.w6.companion

import android.content.Intent
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.compose.runtime.DisposableEffect

/** The only activity. Screens are plain state: Home, Faces, Settings, Log and Firmware, with Back going up one level. */
class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge()
        super.onCreate(savedInstanceState)
        setContent { W6Theme { App(vm, packageManager.getPackageInfo(packageName, 0).versionName ?: "?") } }
        if (savedInstanceState == null) vm.offerLink(intent?.dataString)
    }

    // A w6://pair link (the stand's QR code scanned with the camera app) while W6 is already open
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        vm.offerLink(intent.dataString)
    }

    // While the app is on screen it keeps the link running itself, so the stand is updated even without notification access.
    override fun onStart() { super.onStart(); Link.hold(this, "app") }
    override fun onStop() { Link.release("app"); super.onStop() }
}

private enum class Screen { Home, Faces, Settings, Log, Firmware }

@Composable
private fun App(vm: AppViewModel, appVersion: String) {
    val ui by vm.ui.collectAsStateWithLifecycle()
    val link by Link.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var screen by rememberSaveable { mutableStateOf(Screen.Home) }

    // Poll the stand only while the app is in front, and notice a permission granted in Settings on return.
    val owner = LocalLifecycleOwner.current
    DisposableEffect(owner) {
        val obs = LifecycleEventObserver { _, e ->
            when (e) {
                Lifecycle.Event.ON_RESUME -> { vm.recheckListener(); vm.visible(true) }
                Lifecycle.Event.ON_PAUSE -> vm.visible(false)
                else -> {}
            }
        }
        owner.lifecycle.addObserver(obs)
        onDispose { owner.lifecycle.removeObserver(obs); vm.visible(false) }
    }
    LaunchedEffect(Unit) { vm.messages.collect { snackbar.showSnackbar(it) } }
    LaunchedEffect(screen, ui.paired, ui.onboarded) { vm.watchScreen(screen == Screen.Home && ui.paired && ui.onboarded) }

    if (!ui.paired) {
        val incoming by vm.incoming.collectAsStateWithLifecycle()
        PairScreen(rememberStands(), PairActions(vm::pairStart, vm::pairWithCode, vm::pair), incoming, vm::clearIncoming)
        return
    }
    if (!ui.onboarded) {
        AccessScreen(ui.listenerAllowed, vm::finishOnboarding)
        return
    }

    BackHandler(enabled = screen != Screen.Home) {
        screen = if (screen == Screen.Log || screen == Screen.Firmware) Screen.Settings else Screen.Home
    }
    when (screen) {
        Screen.Home -> HomeScreen(
            ui, link, snackbar,
            onSettings = { screen = Screen.Settings },
            onShare = vm::setShare,
            onFaces = { screen = Screen.Faces },
            onFace = vm::face,
            onChargingStyle = vm::chargingStyle,
            onTimer = vm::timer,
            onCancelTimer = vm::cancelTimer,
            onRefresh = { vm.refresh() },
            onRetry = { vm.findAgain() },
        )
        Screen.Settings -> SettingsScreen(
            ui, snackbar, appVersion,
            onBack = { screen = Screen.Home },
            onRotation = vm::rotation,
            onClock24h = vm::clock24h,
            onFindAgain = { vm.findAgain() },
            onLog = { screen = Screen.Log },
            onFirmware = { screen = Screen.Firmware },
            onReboot = vm::reboot,
            onUnpair = { vm.unpair(); screen = Screen.Home },
        )
        Screen.Faces -> FacesScreen(ui, snackbar, onBack = { screen = Screen.Home }, onFace = vm::face)
        Screen.Log -> LogScreen(ui.log, onBack = { screen = Screen.Settings }, onLoad = { vm.loadLog() })
        Screen.Firmware -> FirmwareScreen(
            ui.ota, ui.status?.version,
            onBack = { screen = Screen.Settings },
            onPick = { vm.pickFirmware(it) },
            onInstall = vm::installFirmware,
            onClear = vm::clearFirmware,
        )
    }
}
