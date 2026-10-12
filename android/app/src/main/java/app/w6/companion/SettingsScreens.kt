package app.w6.companion

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.ListAlt
import androidx.compose.material.icons.outlined.BatteryAlert
import androidx.compose.material.icons.outlined.BugReport
import androidx.compose.material.icons.outlined.Info
import androidx.compose.material.icons.outlined.LinkOff
import androidx.compose.material.icons.outlined.NotificationsActive
import androidx.compose.material.icons.outlined.Power
import androidx.compose.material.icons.outlined.RestartAlt
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material.icons.outlined.ScreenRotation
import androidx.compose.material.icons.outlined.SystemUpdate
import androidx.compose.material.icons.outlined.Wifi
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter

@OptIn(ExperimentalMaterial3Api::class)
@Composable
internal fun SubScreen(title: String, onBack: () -> Unit, snackbar: SnackbarHostState? = null, content: @Composable (Modifier) -> Unit) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title) },
                navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Outlined.ArrowBack, "Back") } },
            )
        },
        snackbarHost = { if (snackbar != null) SnackbarHost(snackbar) },
    ) { pad -> content(Modifier.padding(pad).fillMaxSize()) }
}

// ---------------------------------------------------------------- stand settings

@Composable
fun SettingsScreen(
    ui: AppViewModel.Ui,
    snackbar: SnackbarHostState,
    appVersion: String,
    onBack: () -> Unit,
    onRotation: (Int) -> Unit,
    onClock24h: (Boolean) -> Unit,
    onFindAgain: () -> Unit,
    onLog: () -> Unit,
    onFirmware: () -> Unit,
    onReboot: () -> Unit,
    onUnpair: () -> Unit,
) {
    var confirmReboot by rememberSaveable { mutableStateOf(false) }
    var confirmUnpair by rememberSaveable { mutableStateOf(false) }
    val s = ui.status
    val online = ui.reachable == true
    val context = LocalContext.current

    SubScreen("Stand settings", onBack, snackbar) { mod ->
        LazyColumn(mod) {
            item { SectionHeader("Screen") }
            item {
                ListItem(
                    headlineContent = { Text("Rotation") },
                    supportingContent = { Text("Turn the picture if the text isn't upright") },
                    leadingContent = { Icon(Icons.Outlined.ScreenRotation, null) },
                )
                SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth().padding(start = 56.dp, end = 16.dp, bottom = 8.dp)) {
                    for (q in 0..3) {
                        SegmentedButton(
                            selected = s?.rotation == q, onClick = { onRotation(q) }, enabled = online,
                            shape = SegmentedButtonDefaults.itemShape(q, 4),
                        ) { Text("${q * 90}°") }
                    }
                }
            }
            if (s?.clock24h != null) item {
                SwitchRow("24-hour clock", if (s.clock24h) "21:30" else "9:30 PM", s.clock24h, onClock24h,
                    icon = Icons.Outlined.Schedule, enabled = online)
            }
            item { SectionHeader("Maintenance") }
            item {
                Row2("Start history", "Every restart and why: power cuts, crashes, updates", Icons.AutoMirrored.Outlined.ListAlt, online, onLog)
                Row2("Install firmware", "Update the stand from a firmware file on this phone", Icons.Outlined.SystemUpdate, online, onFirmware)
                Row2("Restart the stand", "Takes about ten seconds", Icons.Outlined.RestartAlt, online) { confirmReboot = true }
            }
            item { SectionHeader("This phone") }
            item {
                ListItem(
                    headlineContent = { Text("Stand address") },
                    supportingContent = { Text(ui.host) },
                    leadingContent = { Icon(Icons.Outlined.Wifi, null) },
                    trailingContent = { TextButton(onClick = onFindAgain) { Text("Find again") } },
                )
                if (ui.listenerAllowed) ListItem(
                    headlineContent = { Text("Notification access") },
                    supportingContent = { Text("On: W6 keeps the stand updated with the app closed") },
                    leadingContent = { Icon(Icons.Outlined.NotificationsActive, null) },
                    modifier = Modifier.clickable(onClickLabel = "Open the setting") { openListenerSettings(context) },
                )
                else BackgroundAccessCard(Modifier.padding(horizontal = 16.dp, vertical = 8.dp))
                ListItem(
                    headlineContent = { Text("Forget this stand", color = MaterialTheme.colorScheme.error) },
                    supportingContent = { Text("Takes this phone's charge and music off the stand") },
                    leadingContent = { Icon(Icons.Outlined.LinkOff, null, tint = MaterialTheme.colorScheme.error) },
                    modifier = Modifier.clickable { confirmUnpair = true },
                )
            }
            item {
                HorizontalDivider(Modifier.padding(top = 16.dp))
                ListItem(
                    headlineContent = { Text("About") },
                    supportingContent = { Text("App $appVersion" + (s?.version?.let { " · stand firmware $it" } ?: "")) },
                    leadingContent = { Icon(Icons.Outlined.Info, null) },
                )
                Spacer(Modifier.height(24.dp))
            }
        }
    }

    if (confirmReboot) Confirm("Restart the stand?", "The screen goes dark for a few seconds, then the clock comes back.", "Restart",
        onDismiss = { confirmReboot = false }) { confirmReboot = false; onReboot() }
    if (confirmUnpair) Confirm("Forget this stand?", "Its screen stops showing this phone's charge and music. To connect " +
        "again, pair the phone with the code the stand shows.", "Forget",
        onDismiss = { confirmUnpair = false }) { confirmUnpair = false; onUnpair() }
}

@Composable
private fun Row2(title: String, sub: String, icon: ImageVector, enabled: Boolean, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(sub) },
        leadingContent = { Icon(icon, null) },
        modifier = Modifier.clickable(enabled = enabled, onClick = onClick),
    )
}

@Composable
private fun Confirm(title: String, text: String, action: String, onDismiss: () -> Unit, onConfirm: () -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(text) },
        confirmButton = { TextButton(onClick = onConfirm) { Text(action) } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

// ---------------------------------------------------------------- start history

private val startedFormat24 = DateTimeFormatter.ofPattern("d MMM, HH:mm:ss")
private val startedFormat12 = DateTimeFormatter.ofPattern("d MMM, h:mm:ss a")

@Composable
fun LogScreen(log: StandLog?, onBack: () -> Unit, onLoad: () -> Unit) {
    LaunchedEffect(Unit) { onLoad() }
    SubScreen("Start history", onBack) { mod ->
        LazyColumn(mod) {
            item {
                Card(
                    Modifier.fillMaxWidth().padding(16.dp),
                    colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerHigh),
                ) {
                    Text(
                        "Several short \"power on\" starts in a row mean the power is cutting out: the cable, the plug, " +
                            "the charger or a joint inside. \"Crash\" or \"watchdog\" means a firmware problem. \"Brownout\" " +
                            "means the supply dipped without cutting out.",
                        style = MaterialTheme.typography.bodyMedium, modifier = Modifier.padding(16.dp),
                    )
                }
            }
            if (log == null) item { Text("Loading…", Modifier.padding(16.dp)) }
            else items(log.boots, key = { it.n }) { b -> BootRow(b) }
        }
    }
}

@Composable
private fun BootRow(b: StandLog.Boot) {
    val h24 = android.text.format.DateFormat.is24HourFormat(LocalContext.current)
    val icon = when (b.reason) {
        "power on" -> Icons.Outlined.Power
        "crash", "watchdog" -> Icons.Outlined.BugReport
        "brownout" -> Icons.Outlined.BatteryAlert
        else -> Icons.Outlined.RestartAlt
    }
    val bad = b.reason in setOf("crash", "watchdog", "brownout")
    val ran = when {
        b.running -> "running for ${Logic.duration(b.ranS)}"
        b.ranExact -> "ran ${Logic.duration(b.ranS)}"
        else -> "ran ${Logic.lostPowerRan(b.ranS)}, then lost power"
    }
    val details = listOfNotNull(
        if (b.gotOnline) "got online" else "never got online",
        if (b.wifiDrops > 0) "${b.wifiDrops} Wi-Fi drop" + (if (b.wifiDrops == 1) "" else "s") else null,
        b.maxChipC?.let { "chip up to $it °C" },
        b.restartedFor?.let { "restarted for $it" },
        if (b.setupByPlugging) "opened setup (plugged in 3 times)" else null,
    )
    ListItem(
        overlineContent = {
            Text("#${b.n} · " + (b.startedUnix?.let { (if (h24) startedFormat24 else startedFormat12).format(Instant.ofEpochSecond(it).atZone(ZoneId.systemDefault())) }
                ?: "before network time"))
        },
        headlineContent = { Text("${b.reason.replaceFirstChar { it.uppercase() }} · $ran") },
        supportingContent = { Text(details.joinToString(" · ")) },
        leadingContent = {
            Icon(icon, null, tint = if (bad) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
        },
    )
}

// ---------------------------------------------------------------- firmware

@Composable
fun FirmwareScreen(ota: AppViewModel.Ota, current: String?, onBack: () -> Unit, onPick: (android.net.Uri) -> Unit,
                   onInstall: () -> Unit, onClear: () -> Unit) {
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri -> uri?.let(onPick) }
    val busy = ota.progress != null && !ota.done
    SubScreen("Install firmware", { if (!busy) { onClear(); onBack() } }) { mod ->
        Column(mod.padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text("Now on the stand: ${current ?: "unknown"}", style = MaterialTheme.typography.titleMedium)
            Text(
                "Choose firmware.bin from a firmware build (firmware/w6, pio run). The stand keeps the old version and " +
                    "goes back to it by itself if the new one crashes or can't get onto the Wi-Fi. Keep this screen open " +
                    "until it finishes.",
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            OutlinedButton(onClick = { picker.launch(arrayOf("*/*")) }, enabled = !busy) { Text("Choose file") }
            if (ota.uri != null) {
                ListItem(
                    headlineContent = { Text(ota.name) },
                    supportingContent = { Text("${ota.size / 1024} KB") },
                    leadingContent = { Icon(Icons.Outlined.SystemUpdate, null) },
                )
            }
            ota.error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
            ota.progress?.let { p ->
                LinearProgressIndicator(progress = { p }, modifier = Modifier.fillMaxWidth())
                Text(if (ota.done) "Sent. The stand is restarting into the new firmware; check the version here in a minute."
                    else "Sending… ${(p * 100).toInt()}%")
            }
            Spacer(Modifier.weight(1f))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.End, verticalAlignment = Alignment.CenterVertically) {
                if (ota.done) Button(onClick = { onClear(); onBack() }) { Text("Done") }
                else Button(onClick = onInstall, enabled = ota.valid && !busy) { Text("Install") }
                Spacer(Modifier.width(0.dp))
            }
        }
    }
}
