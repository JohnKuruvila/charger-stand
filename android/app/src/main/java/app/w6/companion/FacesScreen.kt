package app.w6.companion

import androidx.annotation.DrawableRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items as rowItems
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.outlined.Schedule
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/** The faces the app knows, with a picture from the firmware's desktop preview (res/drawable-nodpi/face_*.webp). */
private val FACES = mapOf(
    "classic" to Face("Classic", R.drawable.face_classic),
    "analog" to Face("Analog", R.drawable.face_analog),
    "stack" to Face("Stack", R.drawable.face_stack),
    "stack_calm" to Face("Stack, calm", R.drawable.face_stack_calm),
    "orbit" to Face("Orbit", R.drawable.face_orbit),
    "orbit_night" to Face("Orbit, night", R.drawable.face_orbit_night),
    "segment" to Face("Segment", R.drawable.face_segment),
    "segment_teal" to Face("Segment, teal", R.drawable.face_segment_teal),
    "words" to Face("Words", R.drawable.face_words),
    "walnut" to Face("Walnut", R.drawable.face_walnut),
    "minimal" to Face("Minimal", R.drawable.face_minimal),
    "terminal" to Face("Terminal", R.drawable.face_terminal),
    "terminal_amber" to Face("Terminal, amber", R.drawable.face_terminal_amber),
    "wormhole" to Face("Wormhole", R.drawable.face_wormhole, moving = true),
    "aurora" to Face("Aurora", R.drawable.face_aurora, moving = true),
    "galaxy" to Face("Galaxy", R.drawable.face_galaxy, moving = true),
    "silk" to Face("Silk", R.drawable.face_silk, moving = true),
    "sky" to Face("Sky", R.drawable.face_sky),
    "walnut_grain" to Face("Walnut grain", R.drawable.face_walnut_grain),
)

/** Firmware that doesn't list its faces has these. */
val OLD_FACES = listOf("classic", "analog")

/** [moving]: the face animates all the time, which keeps the stand busier (about a third to a half of its time). */
internal data class Face(val label: String, @DrawableRes val picture: Int?, val moving: Boolean = false)

/** A face the app has no picture for (newer firmware) gets its name. */
internal fun faceInfo(key: String) = FACES[key] ?: Face(key.replace('_', ' ').replaceFirstChar { it.uppercase() }, null)

/** How the charging screen can look, with a picture from the firmware's desktop preview (charging_*.webp). */
private val CHARGING = mapOf(
    "classic" to Face("Classic", R.drawable.charging_classic),
    "warp" to Face("Warp", R.drawable.charging_warp, moving = true),
    "swarm" to Face("Swarm", R.drawable.charging_swarm, moving = true),
)

internal fun chargingInfo(key: String) = CHARGING[key] ?: faceInfo(key).copy(picture = null)

/** A face's picture in a circle, like the stand's screen. */
@Composable
internal fun FacePicture(key: String, modifier: Modifier = Modifier, info: (String) -> Face = ::faceInfo) {
    val face = info(key)
    Box(modifier.clip(CircleShape).background(Color.Black), contentAlignment = Alignment.Center) {
        if (face.picture != null) Image(painterResource(face.picture), face.label, Modifier.fillMaxSize())
        else Icon(Icons.Outlined.Schedule, face.label, tint = Color.White)
    }
}

/**
 * One face to pick, as watch apps show them: the picture in a ring, its name under it, and a check on the one in use.
 * [size] null: as wide as its cell.
 */
@Composable
internal fun FaceChoice(
    key: String, selected: Boolean, enabled: Boolean, onPick: (String) -> Unit,
    modifier: Modifier = Modifier, size: Dp? = null, info: (String) -> Face = ::faceInfo,
) {
    val face = info(key)
    Column(
        modifier
            .clip(MaterialTheme.shapes.large)
            .selectable(selected = selected, enabled = enabled, role = Role.RadioButton, onClick = { onPick(key) })
            .alpha(if (enabled) 1f else 0.5f)
            .padding(6.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        val ring = if (selected) BorderStroke(3.dp, MaterialTheme.colorScheme.primary)
        else BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)
        Box((if (size != null) Modifier.size(size) else Modifier.fillMaxWidth().aspectRatio(1f))) {
            Box(Modifier.fillMaxSize().border(ring, CircleShape).padding(5.dp)) { FacePicture(key, Modifier.fillMaxSize(), info) }
            if (selected) Icon(
                Icons.Filled.CheckCircle, "In use",
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.align(Alignment.BottomEnd).size(26.dp)
                    .background(MaterialTheme.colorScheme.surface, CircleShape),
            )
        }
        Spacer(Modifier.height(6.dp))
        Text(
            face.label, style = MaterialTheme.typography.bodyMedium, textAlign = TextAlign.Center, maxLines = 1,
            overflow = TextOverflow.Ellipsis, modifier = if (size != null) Modifier.width(size) else Modifier,
            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
        )
    }
}

/** A row of faces to swipe through, the one in use scrolled into view (Home). */
@Composable
internal fun FaceRow(
    keys: List<String>, selected: String?, enabled: Boolean, onPick: (String) -> Unit,
    info: (String) -> Face = ::faceInfo, size: Dp = 84.dp, start: Dp = 10.dp,
) {
    val state = rememberLazyListState()
    LaunchedEffect(keys.size) { keys.indexOf(selected).takeIf { it > 0 }?.let { state.scrollToItem((it - 1).coerceAtLeast(0)) } }
    LazyRow(state = state, contentPadding = PaddingValues(start = start, end = 10.dp), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        rowItems(keys, key = { it }) { k -> FaceChoice(k, k == selected, enabled, onPick, size = size, info = info) }
    }
}

/** Home → Clock faces → See all: every face, still ones first, then the animated ones. */
@Composable
fun FacesScreen(ui: AppViewModel.Ui, snackbar: SnackbarHostState, onBack: () -> Unit, onFace: (String) -> Unit) {
    val keys = ui.status?.faces ?: OLD_FACES
    val (moving, still) = keys.partition { faceInfo(it).moving }
    val selected = ui.status?.face
    val enabled = ui.reachable == true
    SubScreen("Clock faces", onBack, snackbar) { mod ->
        LazyVerticalGrid(
            columns = GridCells.Adaptive(150.dp),
            modifier = mod,
            contentPadding = PaddingValues(start = 10.dp, end = 10.dp, bottom = 24.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            if (moving.isNotEmpty()) item(span = { GridItemSpan(maxLineSpan) }) { SectionHeader("Still", inset = 6.dp) }
            items(still, key = { it }) { k -> FaceChoice(k, k == selected, enabled, onFace) }
            if (moving.isNotEmpty()) {
                item(span = { GridItemSpan(maxLineSpan) }) {
                    Column {
                        SectionHeader("Animated", inset = 6.dp)
                        Text(
                            "Every pixel moves, like a screensaver. These keep the stand busier and a little warmer.",
                            style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                            modifier = Modifier.padding(start = 6.dp, end = 6.dp, bottom = 8.dp),
                        )
                    }
                }
                items(moving, key = { it }) { k -> FaceChoice(k, k == selected, enabled, onFace) }
            }
        }
    }
}
