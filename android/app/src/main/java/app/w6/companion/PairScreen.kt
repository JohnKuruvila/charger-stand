package app.w6.companion

import android.content.Context
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowForward
import androidx.compose.material.icons.outlined.QrCodeScanner
import androidx.compose.material.icons.outlined.Visibility
import androidx.compose.material.icons.outlined.VisibilityOff
import androidx.compose.material.icons.outlined.WatchLater
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

/** What the pairing screen needs from the ViewModel. */
class PairActions(
    val start: suspend (host: String) -> AppViewModel.PairStart,
    val code: suspend (host: String, code: String, name: String) -> String?,
    val token: suspend (host: String, token: String, name: String) -> String?,
)

/**
 * First run. Pick the stand (found on the Wi-Fi, or typed), and it shows a one-time code; scan it or type it, and the
 * stand hands over its token. Older firmware (before 0.3.2) has no codes, so the token is typed instead.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PairScreen(
    found: List<Discovery.Found>,
    actions: PairActions,
    incoming: Logic.PairLink? = null,
    onIncomingDone: () -> Unit = {},
    previewStep: String? = null,  // screenshot tests: "code" or "token"
) {
    var host by rememberSaveable { mutableStateOf(if (previewStep != null) "192.168.0.50" else "") }
    var name by rememberSaveable { mutableStateOf("") }
    var step by rememberSaveable { mutableStateOf(previewStep ?: "pick") }  // pick → code (or token)
    var seconds by rememberSaveable { mutableIntStateOf(120) }
    var shownAt by rememberSaveable { mutableLongStateOf(0L) }  // restarts the countdown for each new code
    var busy by remember { mutableStateOf(false) }
    var error by remember { mutableStateOf<String?>(null) }
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()

    fun run(block: suspend () -> String?) {
        if (busy) return
        busy = true
        error = null
        scope.launch { error = block(); busy = false }
    }

    fun start(h: String, n: String) {
        host = h; name = n
        run {
            when (val r = actions.start(h)) {
                is AppViewModel.PairStart.Showing -> {
                    host = r.host; seconds = r.seconds; shownAt = System.currentTimeMillis(); step = "code"; null
                }
                is AppViewModel.PairStart.OldFirmware -> { host = r.host; step = "token"; null }
                is AppViewModel.PairStart.Error -> r.message
            }
        }
    }

    fun sendCode(h: String, code: String) = run { actions.code(h, code, name.ifEmpty { "w6" }) }

    Scaffold(
        modifier = Modifier.nestedScroll(scroll.nestedScrollConnection),
        topBar = { LargeTopAppBar(title = { Text("Connect to your W6") }, scrollBehavior = scroll) },
    ) { pad ->
        LazyColumn(Modifier.fillMaxSize().imePadding(), contentPadding = pad) {
            when (step) {
                "pick" -> {
                    item {
                        Text(
                            "W6 shows your phone's charge and what's playing on the stand, and lets you " +
                                "change its clock face and timer. Keep the phone on the same Wi-Fi as the stand.",
                            style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(horizontal = 16.dp),
                        )
                    }
                    item { SectionHeader("Stands on this Wi-Fi") }
                    if (found.isEmpty()) item {
                        ListItem(
                            headlineContent = { Text("Looking for stands…") },
                            supportingContent = { Text("Nothing yet? Type its address below.") },
                            leadingContent = { CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 3.dp) },
                        )
                    }
                    items(found, key = { it.name }) { f ->
                        ListItem(
                            headlineContent = { Text(f.name.uppercase()) },
                            supportingContent = { Text(f.host) },
                            leadingContent = { Icon(Icons.Outlined.WatchLater, null) },
                            trailingContent = { Icon(Icons.AutoMirrored.Outlined.ArrowForward, "Pair") },
                            modifier = Modifier.clickable(enabled = !busy) { start(f.host, f.name) },
                        )
                    }
                    item {
                        Column(Modifier.padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                            Spacer(Modifier.height(8.dp))
                            OutlinedTextField(
                                value = host, onValueChange = { host = it; name = "" },
                                label = { Text("Or type its address") }, placeholder = { Text("w6.local or 192.168.0.50") },
                                singleLine = true, modifier = Modifier.fillMaxWidth(),
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Go),
                                keyboardActions = KeyboardActions(onGo = { start(host, "") }),
                            )
                            ErrorText(error)
                            Button(onClick = { start(host, "") }, enabled = !busy && host.isNotBlank(),
                                modifier = Modifier.fillMaxWidth().height(48.dp)) { BusyLabel(busy, "Pair") }
                        }
                    }
                }
                "code" -> item {
                    CodeStep(
                        host = host, seconds = seconds, shownAt = shownAt, busy = busy, error = error,
                        onScan = {
                            scanQr(context, onText = { text ->
                                val link = text?.let(Logic::pairLink)
                                when {
                                    text == null -> {}
                                    link != null -> sendCode(link.host, link.code)
                                    Logic.pairCode(text) != null -> sendCode(host, text)
                                    else -> error = "That isn't a W6 pairing code."
                                }
                            }, onError = { error = it })
                        },
                        onCode = { sendCode(host, it) },
                        onExpired = { error = "The code has expired. Start again."; },
                        onRestart = { start(host, name) },
                        onBack = { step = "pick"; error = null },
                    )
                }
                "token" -> item {
                    TokenStep(host, busy, error, onToken = { h, t -> run { actions.token(h, t, name.ifEmpty { "w6" }) } },
                        onBack = { step = "pick"; error = null })
                }
            }
            if (step == "pick") item {
                TextButton(onClick = { step = "token"; error = null }, modifier = Modifier.padding(horizontal = 8.dp, vertical = 16.dp)) {
                    Text("Use a token instead")
                }
            }
        }
    }

    if (incoming != null) {
        AlertDialog(
            onDismissRequest = onIncomingDone,
            icon = { Icon(Icons.Outlined.QrCodeScanner, null) },
            title = { Text("Pair with this stand?") },
            text = { Text("A pairing code for the W6 stand at ${incoming.host} was opened. Pair this phone with it?") },
            confirmButton = { TextButton(onClick = { onIncomingDone(); host = incoming.host; sendCode(incoming.host, incoming.code) }) { Text("Pair") } },
            dismissButton = { TextButton(onClick = onIncomingDone) { Text("Cancel") } },
        )
    }
}

@Composable
private fun CodeStep(
    host: String, seconds: Int, shownAt: Long, busy: Boolean, error: String?,
    onScan: () -> Unit, onCode: (String) -> Unit, onExpired: () -> Unit, onRestart: () -> Unit, onBack: () -> Unit,
) {
    var code by rememberSaveable { mutableStateOf("") }
    var left by remember(shownAt) { mutableFloatStateOf(1f) }
    LaunchedEffect(shownAt) {
        val total = seconds * 1000L
        var t = 0L
        while (t < total) { delay(250); t += 250; left = 1f - t.toFloat() / total }
        onExpired()
    }
    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
            Column(Modifier.padding(20.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Icon(Icons.Outlined.QrCodeScanner, null, Modifier.size(48.dp), tint = MaterialTheme.colorScheme.onSecondaryContainer)
                Spacer(Modifier.height(12.dp))
                Text("Look at the stand", style = MaterialTheme.typography.titleLarge, color = MaterialTheme.colorScheme.onSecondaryContainer)
                Spacer(Modifier.height(4.dp))
                Text("It's showing a code. Scan it, or type the 8 characters under it.", textAlign = TextAlign.Center,
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSecondaryContainer)
                Spacer(Modifier.height(16.dp))
                LinearProgressIndicator(progress = { left }, modifier = Modifier.fillMaxWidth())
            }
        }
        Button(onClick = onScan, enabled = !busy && left > 0f, modifier = Modifier.fillMaxWidth().height(56.dp)) {
            Icon(Icons.Outlined.QrCodeScanner, null)
            Spacer(Modifier.width(8.dp))
            BusyLabel(busy, "Scan the code")
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                code, { code = it.uppercase().take(9) }, singleLine = true, modifier = Modifier.weight(1f),
                label = { Text("Or type the code") }, placeholder = { Text("K7M2 P9XQ") },
                textStyle = MaterialTheme.typography.titleMedium.copy(fontFamily = FontFamily.Monospace),
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters,
                    keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Done, autoCorrectEnabled = false),
                keyboardActions = KeyboardActions(onDone = { onCode(code) }),
            )
            Spacer(Modifier.width(8.dp))
            OutlinedButton(onClick = { onCode(code) }, enabled = !busy && Logic.pairCode(code) != null && left > 0f) { Text("Pair") }
        }
        ErrorText(error)
        Row {
            TextButton(onClick = onBack) { Text("Back") }
            Spacer(Modifier.weight(1f))
            TextButton(onClick = onRestart, enabled = !busy) { Text("Show a new code") }
        }
        Text("Stand: $host", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
private fun TokenStep(host: String, busy: Boolean, error: String?, onToken: (String, String) -> Unit, onBack: () -> Unit) {
    var address by rememberSaveable { mutableStateOf(host) }
    var token by rememberSaveable { mutableStateOf("") }
    var show by rememberSaveable { mutableStateOf(false) }
    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Stands with firmware older than 0.3.2 can't show a pairing code. Enter the token the setup page showed.",
            style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        OutlinedTextField(address, { address = it }, label = { Text("Stand address") }, singleLine = true, modifier = Modifier.fillMaxWidth())
        OutlinedTextField(
            value = token, onValueChange = { token = it }, label = { Text("Token") }, singleLine = true, modifier = Modifier.fillMaxWidth(),
            visualTransformation = if (show) VisualTransformation.None else PasswordVisualTransformation(),
            trailingIcon = {
                IconButton(onClick = { show = !show }) {
                    Icon(if (show) Icons.Outlined.VisibilityOff else Icons.Outlined.Visibility, if (show) "Hide token" else "Show token")
                }
            },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
        )
        ErrorText(error)
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("Back") }
            Spacer(Modifier.weight(1f))
            Button(onClick = { onToken(address, token) }, enabled = !busy && address.isNotBlank() && token.isNotBlank()) {
                BusyLabel(busy, "Connect")
            }
        }
    }
}

@Composable
private fun ErrorText(error: String?) {
    error?.let { Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodyMedium) }
}

@Composable
private fun BusyLabel(busy: Boolean, text: String) {
    if (busy) CircularProgressIndicator(Modifier.size(20.dp), strokeWidth = 2.dp, color = MaterialTheme.colorScheme.onPrimary)
    else Text(text)
}

/** Google's code scanner (Play services): its own camera screen, so W6 needs no camera permission. */
private fun scanQr(context: Context, onText: (String?) -> Unit, onError: (String) -> Unit) {
    val options = GmsBarcodeScannerOptions.Builder().setBarcodeFormats(Barcode.FORMAT_QR_CODE).enableAutoZoom().build()
    GmsBarcodeScanning.getClient(context, options).startScan()
        .addOnSuccessListener { onText(it.rawValue) }
        .addOnCanceledListener { onText(null) }
        .addOnFailureListener { onError("The scanner isn't available on this phone. Type the code instead.") }
}

/** After pairing: the one permission that matters, explained, with a way past it. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AccessScreen(allowed: Boolean, onDone: () -> Unit) {
    Scaffold(topBar = { LargeTopAppBar(title = { Text(if (allowed) "All set" else "One more thing") }) }) { pad ->
        Column(Modifier.padding(pad).padding(16.dp).fillMaxSize(), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            if (allowed) {
                Text("W6 will keep the stand updated in the background: your charge when the phone is on it and what's " +
                    "playing. You can change what it shares on the next screen.",
                    style = MaterialTheme.typography.bodyLarge)
            } else {
                BackgroundAccessCard()
                Text("Without it, W6 updates the stand only while the app is open, and can't show what's playing.",
                    style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Spacer(Modifier.weight(1f))
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.CenterEnd) {
                if (allowed) Button(onClick = onDone) { Text("Continue") } else TextButton(onClick = onDone) { Text("Skip for now") }
            }
        }
    }
}

@Composable
fun rememberStands(): List<Discovery.Found> {
    val c = LocalContext.current
    val flow = remember { Discovery(c).stands() }
    val list by flow.collectAsStateWithLifecycle(initialValue = emptyList())
    return list
}
