package app.w6.companion

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.NotificationsActive
import androidx.compose.material.icons.outlined.WifiOff
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ListItem
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.foundation.selection.toggleable
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/** The stand's round screen as it looks right now (GET /api/screenshot), in a walnut-coloured bezel. */
@Composable
fun StandScreen(image: ImageBitmap?, reachable: Boolean?, size: Dp, modifier: Modifier = Modifier) {
    val bezel = Color(0xFF5C3A21)
    Box(
        modifier
            .size(size)
            .border(size * 0.045f, bezel, CircleShape)
            .padding(size * 0.045f)
            .clip(CircleShape)
            .background(Color.Black)
            .semantics { contentDescription = "What the stand's screen shows now" },
        contentAlignment = Alignment.Center,
    ) {
        when {
            image != null -> Image(image, contentDescription = null, filterQuality = FilterQuality.Medium,
                modifier = Modifier.size(size), alpha = if (reachable == false) 0.35f else 1f)
            reachable == false -> Icon(Icons.Outlined.WifiOff, null, tint = Color(0xFF8C8C8C), modifier = Modifier.size(size / 5))
            else -> CircularProgressIndicator(color = Color(0xFFFFA94D), modifier = Modifier.size(size / 6))
        }
    }
}

/** A section's title, and optionally a link at the end of the line ("See all"), as in Android's own settings. */
@Composable
fun SectionHeader(
    text: String, modifier: Modifier = Modifier, action: String? = null, onAction: () -> Unit = {}, inset: Dp = 16.dp,
) {
    Row(
        modifier.fillMaxWidth().padding(start = inset, end = 4.dp, top = if (action != null) 12.dp else 24.dp,
            bottom = if (action != null) 0.dp else 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.weight(1f).semantics { heading() })
        if (action != null) TextButton(onClick = onAction) { Text(action) }
    }
}

/** A list row with a switch, where the whole row toggles it (as in Android's settings), and a screen reader hears one switch. */
@Composable
fun SwitchRow(
    title: String, subtitle: String?, checked: Boolean, onChange: (Boolean) -> Unit,
    icon: ImageVector? = null, enabled: Boolean = true, indent: Boolean = false,
) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = subtitle?.let { { Text(it) } },
        leadingContent = when {
            icon != null -> { { Icon(icon, null) } }
            indent -> { { Spacer(Modifier.size(24.dp)) } }
            else -> null
        },
        trailingContent = { Switch(checked, onCheckedChange = null, enabled = enabled) },
        modifier = Modifier.toggleable(checked, enabled = enabled, role = Role.Switch, onValueChange = onChange),
    )
}

/** Asks for notification access, which lets W6 run with the app closed and see what's playing. */
@Composable
fun BackgroundAccessCard(modifier: Modifier = Modifier, onDismiss: (() -> Unit)? = null) {
    val c = LocalContext.current
    Card(
        modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer),
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Outlined.NotificationsActive, null, tint = MaterialTheme.colorScheme.onSecondaryContainer)
                Spacer(Modifier.width(12.dp))
                Text("Keep the stand updated with the app closed", style = MaterialTheme.typography.titleMedium,
                    color = MaterialTheme.colorScheme.onSecondaryContainer)
            }
            Spacer(Modifier.height(8.dp))
            Text(
                "Turn on notification access for W6. Android uses it to keep W6 running in the background and to let " +
                    "it see what's playing. W6 doesn't read, keep or send your notifications.",
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSecondaryContainer,
            )
            Spacer(Modifier.height(4.dp))
            Text(
                "If Android says the setting is restricted: open App info, tap ⋮, choose \"Allow restricted settings\", " +
                    "then come back and try again.",
                style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSecondaryContainer,
            )
            Spacer(Modifier.height(12.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Button(onClick = { openListenerSettings(c) }) { Text("Turn on") }
                Spacer(Modifier.width(8.dp))
                TextButton(onClick = { openAppInfo(c) }) { Text("App info") }
                Spacer(Modifier.weight(1f))
                if (onDismiss != null) TextButton(onClick = onDismiss) { Text("Not now") }
            }
        }
    }
}

fun openListenerSettings(c: Context) {
    val cn = ComponentName(c, LinkService::class.java)
    val detail = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R)
        Intent(Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS)
            .putExtra(Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME, cn.flattenToString()) else null
    val general = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
    runCatching { c.startActivity((detail ?: general).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
        .onFailure { runCatching { c.startActivity(general.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) } }
}

fun openAppInfo(c: Context) {
    runCatching {
        c.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", c.packageName, null))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
    }
}
