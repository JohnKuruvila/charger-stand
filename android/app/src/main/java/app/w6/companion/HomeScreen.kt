package app.w6.companion

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.BatteryChargingFull
import androidx.compose.material.icons.outlined.MusicNote
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.Timer
import androidx.compose.material3.AlertDialog
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.pulltorefresh.PullToRefreshBox
import androidx.compose.ui.graphics.Color
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SuggestionChip
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun HomeScreen(
    ui: AppViewModel.Ui,
    link: Link.State,
    snackbar: SnackbarHostState,
    onSettings: () -> Unit,
    onShare: (Prefs.Share, Boolean) -> Unit,
    onFaces: () -> Unit,
    onFace: (String) -> Unit,
    onChargingStyle: (String) -> Unit,
    onTimer: (Int, String) -> Unit,
    onCancelTimer: () -> Unit,
    onRefresh: () -> Unit,
    onRetry: () -> Unit,
) {
    val scroll = TopAppBarDefaults.enterAlwaysScrollBehavior()
    var customTimer by rememberSaveable { mutableStateOf(false) }
    val s = ui.status
    val online = ui.reachable == true

    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = {
            TopAppBar(
                title = { Text("W6") },
                actions = { IconButton(onClick = onSettings) { Icon(Icons.Outlined.Settings, "Stand settings") } },
                scrollBehavior = scroll,
            )
        },
        snackbarHost = { SnackbarHost(snackbar) },
    ) { pad ->
        PullToRefreshBox(ui.refreshing, onRefresh, Modifier.padding(pad).fillMaxSize()) {
            LazyColumn(Modifier.fillMaxSize()) {
                item { StandCard(ui, onRetry) }
                if (!ui.listenerAllowed) item { BackgroundAccessCard(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) }

                item { SectionHeader("Clock faces", action = "See all", onAction = onFaces) }
                item { FaceRow(s?.faces ?: OLD_FACES, s?.face, online, onFace) }

                item { SectionHeader("When the phone charges on the stand") }
                item {
                    val on = ui.shares[Prefs.Share.Battery] == true
                    val p = link.phone
                    SwitchRow(
                        "Show its charge",
                        when {
                            !on -> "Off"
                            p?.charging == true -> listOfNotNull("Now ${p.percent}%", p.watts?.let { "$it W into the battery" }).joinToString(" · ")
                            else -> "The stand shows the percentage while the phone charges"
                        },
                        on, { onShare(Prefs.Share.Battery, it) }, icon = Icons.Outlined.BatteryChargingFull,
                    )
                }
                if (ui.shares[Prefs.Share.Battery] == true) {
                    item {
                        SwitchRow("Wireless charging only", "Ignore charging by cable", ui.shares[Prefs.Share.WirelessOnly] == true,
                            { onShare(Prefs.Share.WirelessOnly, it) }, indent = true)
                    }
                    if (s?.has("charging_styles") == true) item {
                        Text("Charging screen", style = MaterialTheme.typography.bodyLarge,
                            modifier = Modifier.padding(start = 56.dp, top = 8.dp, bottom = 4.dp))
                        FaceRow(s.chargingStyles ?: listOf("classic"), s.chargingStyle ?: "classic", online, onChargingStyle,
                            info = ::chargingInfo, size = 72.dp, start = 50.dp)
                    }
                }

                item { SectionHeader("Music") }
                item {
                    val supported = s?.has("media") != false
                    val enabled = ui.listenerAllowed && supported
                    SwitchRow(
                        "Show what's playing",
                        when {
                            !supported -> "Needs stand firmware 0.3.0 or later"
                            !ui.listenerAllowed -> "Needs notification access (above)"
                            ui.shares[Prefs.Share.Media] != true -> "Off"
                            link.track != null -> (if (link.playing) "Now playing · " else "Paused · ") + link.track
                            else -> "Song, artist and album art, while music plays"
                        },
                        ui.shares[Prefs.Share.Media] == true && enabled, { onShare(Prefs.Share.Media, it) },
                        icon = Icons.Outlined.MusicNote, enabled = enabled,
                    )
                }

                item { SectionHeader("Focus timer") }
                item {
                    val left = ui.timerLeftS
                    if (left != null) {
                        ListItem(
                            headlineContent = { Text(if (left > 0) "${Logic.clockDuration(left)} left" else "Time's up") },
                            supportingContent = { Text("On the stand now") },
                            leadingContent = { Icon(Icons.Outlined.Timer, null) },
                            trailingContent = { OutlinedButton(onClick = onCancelTimer) { Text("Stop") } },
                        )
                    } else {
                        FlowRow(Modifier.padding(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            for (m in listOf(5, 15, 25, 45)) {
                                SuggestionChip(onClick = { onTimer(m, "Focus") }, label = { Text("$m min") }, enabled = online)
                            }
                            SuggestionChip(onClick = { customTimer = true }, label = { Text("Custom…") }, enabled = online)
                        }
                    }
                }

                item { Spacer(Modifier.height(32.dp)) }
            }
        }
    }

    if (customTimer) TimerDialog(onDismiss = { customTimer = false }, onStart = { m, l -> customTimer = false; onTimer(m, l) })
}

@Composable
private fun StandCard(ui: AppViewModel.Ui, onRetry: () -> Unit) {
    val s = ui.status
    ElevatedCard(Modifier.fillMaxWidth().padding(16.dp)) {
        Column(Modifier.fillMaxWidth().padding(vertical = 24.dp, horizontal = 16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            StandScreen(ui.screen, ui.reachable, 220.dp)
            Spacer(Modifier.height(16.dp))
            when (ui.reachable) {
                false -> {
                    Text("Can't reach the stand", style = MaterialTheme.typography.titleMedium)
                    Text("Is the phone on the same Wi-Fi? The stand may also be unplugged.", textAlign = TextAlign.Center,
                        style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(12.dp))
                    FilledTonalButton(onClick = onRetry) { Text("Try again") }
                }
                true -> {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(8.dp).background(Color(0xFF52D273), CircleShape))
                        Spacer(Modifier.width(8.dp))
                        Text("Connected", style = MaterialTheme.typography.titleMedium)
                    }
                    if (s != null) {
                        Spacer(Modifier.height(4.dp))
                        Text(
                            listOfNotNull(
                                s.rssiDbm?.let { "Wi-Fi ${signal(it)}" },
                                s.chipTempC?.let { "${it.roundToInt()} °C" },
                                "up ${Logic.duration(s.uptimeS)}",
                            ).joinToString(" · "),
                            style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
                null -> Text("Connecting…", style = MaterialTheme.typography.titleMedium)
            }
        }
    }
}

private fun signal(dbm: Int) = when {
    dbm >= -55 -> "strong"
    dbm >= -70 -> "good"
    else -> "weak"
}


@Composable
private fun TimerDialog(onDismiss: () -> Unit, onStart: (Int, String) -> Unit) {
    var minutes by rememberSaveable { mutableFloatStateOf(30f) }
    var label by rememberSaveable { mutableStateOf("Focus") }
    AlertDialog(
        onDismissRequest = onDismiss,
        icon = { Icon(Icons.Outlined.Timer, null) },
        title = { Text("Timer") },
        text = {
            Column {
                Text("${minutes.roundToInt()} minutes", style = MaterialTheme.typography.headlineSmall)
                Slider(minutes, { minutes = it }, valueRange = 1f..120f)
                OutlinedTextField(label, { label = it.take(24) }, label = { Text("Label") }, singleLine = true)
            }
        },
        confirmButton = { TextButton(onClick = { onStart(minutes.roundToInt(), label.ifBlank { "Timer" }) }) { Text("Start") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}
