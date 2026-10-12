package app.w6.companion

import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.BatteryManager
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.core.graphics.scale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * Keeps the stand up to date with the phone: the battery while it charges and what's playing.
 *
 * It runs while something holds it: LinkService (the notification listener, which Android keeps running once the user
 * has allowed it, so this works with the app closed) or the app's own screen. Everything here happens on the main
 * thread except the network calls and the album-art scaling.
 */
@SuppressLint("StaticFieldLeak")  // holds only the application context
object Link {
    data class State(
        val running: Boolean = false,
        val background: Boolean = false,         // LinkService holds it: works with the app closed
        val onWifi: Boolean = false,
        val phone: Logic.PhoneReport? = null,    // the last battery report sent
        val phoneSentAtMs: Long = 0,
        val track: String? = null,               // "Title · Artist" of what's playing, if shared
        val playing: Boolean = false,
        val lastOkAtMs: Long = 0,
        val lastError: String? = null,
    )

    private val _state = MutableStateFlow(State())
    val state: StateFlow<State> = _state

    private val holders = mutableSetOf<String>()
    private var scope: CoroutineScope? = null
    private lateinit var app: Context  // the application context, which lives as long as the process
    private val main = Handler(Looper.getMainLooper())

    /** [who]: "service" or "app". The link starts with the first holder and stops when the last one lets go. */
    fun hold(context: Context, who: String) {
        app = context.applicationContext
        val first = holders.isEmpty()
        holders += who
        _state.update { it.copy(running = true, background = "service" in holders) }
        if (first) start() else if (who == "service") media.start()  // the listener allows media sessions
    }

    fun release(who: String) {
        holders -= who
        _state.update { it.copy(running = holders.isNotEmpty(), background = "service" in holders) }
        if (holders.isEmpty()) stop() else if (who == "service") media.stop()
    }

    /** A setting changed in the app: send what it affects now. */
    fun refresh() {
        val s = scope ?: return
        s.launch { battery.evaluate(force = true) }
        s.launch { media.push(force = true) }
    }

    /** The Wi-Fi network while the link runs, for calls that must reach the stand on the home network. */
    fun network(): Network? = wifi.network

    fun listenerAllowed(c: Context) = NotificationManagerCompat.getEnabledListenerPackages(c).contains(c.packageName)

    private fun start() {
        val s = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
        scope = s
        wifi.start()
        battery.start()
        if (listenerAllowed(app)) media.start()
    }

    private fun stop() {
        media.stop()
        battery.stop()
        wifi.stop()
        scope?.cancel()
        scope = null
    }

    // ---------------------------------------------------------------- talking to the stand

    private var lastRediscovery = 0L
    private var misses = 0  // searches in a row that found nothing
    @Volatile private var down = false  // the last call couldn't reach the stand

    /** The API on the Wi-Fi network, or null when not paired or not on Wi-Fi (the stand is only on the home network). */
    private fun api(): StandApi? {
        if (!Prefs.paired(app)) return null
        val net = wifi.network ?: return null
        return StandApi(Prefs.host(app), Prefs.token(app), net.socketFactory)
    }

    /** Runs [block] against the stand and records the outcome; finds the stand again if its address changed. */
    private suspend fun <T> withStand(block: suspend (StandApi) -> T): T? {
        val api = api() ?: return null
        return try {
            block(api).also {
                _state.update { s -> s.copy(lastOkAtMs = System.currentTimeMillis(), lastError = null) }
                // back after an outage: the stand may have restarted and forgotten the phone and the music
                if (down) { down = false; misses = 0; refresh() }
            }
        } catch (e: StandException) {
            _state.update { s -> s.copy(lastError = describe(e)) }
            if (e is StandException.Unreachable) { down = true; findAgain() }
            null
        }
    }

    /**
     * Looks for the stand by name on the network, since its address can change (the router hands out addresses).
     * Unless [force], it waits 30 s after a search that found nothing, doubling up to 5 minutes, so a phone charging
     * on another Wi-Fi doesn't search all day. Returns the address found, or null.
     */
    suspend fun findAgain(force: Boolean = false): String? {
        if (!::app.isInitialized) return null
        val now = SystemClock.elapsedRealtime()
        val name = Prefs.serviceName(app)
        val wait = minOf(30_000L shl misses.coerceAtMost(4), 5 * 60_000L)
        if (name.isEmpty() || (!force && now - lastRediscovery < wait)) return null
        lastRediscovery = now
        val host = Discovery(app).find(name)
        if (host == null) { misses++; return null }
        misses = 0
        if (host != Prefs.host(app)) Prefs.setHost(app, host)
        return host
    }

    fun describe(e: StandException): String = when (e) {
        is StandException.Unreachable -> if (e.slow) "The stand is slow to answer" else "The stand isn't answering"
        is StandException.Unauthorized -> "The stand turned down the token"
        is StandException.Http -> e.message ?: "The stand said no"
    }

    // ---------------------------------------------------------------- Wi-Fi

    private val wifi = object {
        @Volatile var network: Network? = null
        private val cm get() = app.getSystemService(ConnectivityManager::class.java)
        private val callback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(n: Network) {
                network = n
                _state.update { it.copy(onWifi = true) }
                main.post { refresh() }  // back home: bring the stand up to date
            }
            override fun onLost(n: Network) {
                if (network == n) network = null
                _state.update { it.copy(onWifi = network != null) }
            }
        }

        fun start() {
            val req = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build()
            runCatching { cm.registerNetworkCallback(req, callback) }
        }

        fun stop() {
            runCatching { cm.unregisterNetworkCallback(callback) }
            network = null
        }
    }

    // ---------------------------------------------------------------- battery

    private val battery = object {
        private var last: Logic.PhoneReport? = null
        private var lastAt = 0L
        private var heartbeat: Job? = null
        private val lock = Mutex()
        private val receiver = object : BroadcastReceiver() {
            override fun onReceive(c: Context, i: Intent) { scope?.launch { evaluate() } }
        }

        fun start() {
            val f = IntentFilter().apply {
                addAction(Intent.ACTION_BATTERY_CHANGED)
                addAction(Intent.ACTION_POWER_CONNECTED)
                addAction(Intent.ACTION_POWER_DISCONNECTED)
            }
            ContextCompat.registerReceiver(app, receiver, f, ContextCompat.RECEIVER_NOT_EXPORTED)
        }

        fun stop() {
            runCatching { app.unregisterReceiver(receiver) }
            heartbeat?.cancel()
            heartbeat = null
            last = null
        }

        /** Reads the battery now and sends it if the stand needs to know. */
        suspend fun evaluate(force: Boolean = false): Unit = lock.withLock {
            val i = app.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)) ?: return@withLock
            val level = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
            val scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
            if (level < 0 || scale <= 0) return@withLock
            val plugged = i.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0)
            val status = i.getIntExtra(BatteryManager.EXTRA_STATUS, BatteryManager.BATTERY_STATUS_UNKNOWN)
            val charging = plugged != 0 &&
                (status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL)
            val bm = app.getSystemService(BatteryManager::class.java)
            val current = Logic.microAmps(bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CURRENT_NOW))
            Logic.learnDischargeSign(current, plugged != 0)?.let { if (it != Prefs.dischargeSign(app)) Prefs.setDischargeSign(app, it) }

            // "On the stand" means charging wirelessly, unless the user wants any charger to count
            val onStand = !Prefs.get(app, Prefs.Share.WirelessOnly) || plugged == BatteryManager.BATTERY_PLUGGED_WIRELESS
            val share = Prefs.get(app, Prefs.Share.Battery)
            val voltage = i.getIntExtra(BatteryManager.EXTRA_VOLTAGE, -1).takeIf { it > 0 }
            val report = Logic.PhoneReport(
                percent = level * 100 / scale,
                charging = charging && onStand && share,
                watts = if (charging) Logic.chargeWatts(current, voltage, Prefs.dischargeSign(app)) else null,
            )
            val now = SystemClock.elapsedRealtime()
            val wasCharging = last?.charging == true
            val needed = Logic.shouldSendPhone(last, lastAt, report, now) || (force && (report.charging || last?.charging == true))
            // a phone that was never on the stand has nothing to tell it
            if (needed && (report.charging || last?.charging == true || (force && last != null))) {
                if (withStand { it.phone(report) } != null) {
                    last = report
                    lastAt = now
                    _state.update { it.copy(phone = report, phoneSentAtMs = System.currentTimeMillis()) }
                }
            } else if (last == null && !report.charging) {
                last = report  // nothing to clear on the stand
                lastAt = now
            }
            keepAlive(report.charging)
        }

        private fun keepAlive(charging: Boolean) {
            if (charging && heartbeat?.isActive != true) {
                heartbeat = scope?.launch {
                    while (isActive) { delay(Logic.PHONE_HEARTBEAT_MS); evaluate() }
                }
            } else if (!charging) {
                heartbeat?.cancel()
                heartbeat = null
            }
        }
    }

    // ---------------------------------------------------------------- now playing

    private val media = object {
        private var controller: MediaController? = null
        private var sentArt = ""
        private var cleared = true
        private var pending: Job? = null
        private var heartbeat: Job? = null
        private val lock = Mutex()
        private val msm get() = app.getSystemService(MediaSessionManager::class.java)
        private val component get() = ComponentName(app, LinkService::class.java)
        private var started = false

        private val sessions = MediaSessionManager.OnActiveSessionsChangedListener { list -> pick(list.orEmpty()) }
        private val callback = object : MediaController.Callback() {
            override fun onMetadataChanged(metadata: MediaMetadata?) = soon()
            override fun onPlaybackStateChanged(state: PlaybackState?) = soon()
            override fun onSessionDestroyed() = pick(runCatching { msm.getActiveSessions(component) }.getOrDefault(emptyList()))
        }

        fun start() {
            if (started || scope == null) return
            try {
                msm.addOnActiveSessionsChangedListener(sessions, component, main)
                pick(msm.getActiveSessions(component))
                started = true
            } catch (_: SecurityException) {
                // notification access not granted (yet): media stays off
            }
        }

        fun stop() {
            if (!started) return
            started = false
            runCatching { msm.removeOnActiveSessionsChangedListener(sessions) }
            controller?.unregisterCallback(callback)
            controller = null
            heartbeat?.cancel()
            pending?.cancel()
            scope?.launch { if (!cleared) withStand { it.clearMedia() }; cleared = true }
        }

        /** Follows the session that is playing, or else the most recent one. */
        private fun pick(list: List<MediaController>) {
            val next = list.firstOrNull { it.playbackState?.state == PlaybackState.STATE_PLAYING } ?: list.firstOrNull()
            if (next?.sessionToken != controller?.sessionToken) {
                controller?.unregisterCallback(callback)
                controller = next
                next?.registerCallback(callback, main)
            }
            soon()
        }

        /** Many callbacks arrive together when a track changes; send once they settle. */
        private fun soon() {
            pending?.cancel()
            pending = scope?.launch { delay(250); push() }
        }

        suspend fun push(force: Boolean = false): Unit = lock.withLock {
            val c = controller
            val st = c?.playbackState
            val md = c?.metadata
            val share = Prefs.get(app, Prefs.Share.Media) && started
            val active = st != null && md != null &&
                st.state != PlaybackState.STATE_NONE && st.state != PlaybackState.STATE_STOPPED && st.state != PlaybackState.STATE_ERROR
            if (!share || !active) {
                heartbeat?.cancel()
                if (!cleared || force) { if (withStand { it.clearMedia() } != null) cleared = true }
                _state.update { it.copy(track = null, playing = false) }
                return@withLock
            }
            val title = Logic.standText(md.getString(MediaMetadata.METADATA_KEY_TITLE)
                ?: md.getString(MediaMetadata.METADATA_KEY_DISPLAY_TITLE) ?: "", 60).ifEmpty { "Now playing" }
            val artist = Logic.standText(md.getString(MediaMetadata.METADATA_KEY_ARTIST)
                ?: md.getString(MediaMetadata.METADATA_KEY_ALBUM_ARTIST)
                ?: md.getString(MediaMetadata.METADATA_KEY_DISPLAY_SUBTITLE) ?: "", 40)
            val playing = st.state == PlaybackState.STATE_PLAYING || st.state == PlaybackState.STATE_BUFFERING
            val position = Logic.positionNow(st.position, st.lastPositionUpdateTime, st.playbackSpeed, SystemClock.elapsedRealtime(), playing)
            val duration = md.getLong(MediaMetadata.METADATA_KEY_DURATION)
            val art: Bitmap? = md.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)
                ?: md.getBitmap(MediaMetadata.METADATA_KEY_ART) ?: md.getBitmap(MediaMetadata.METADATA_KEY_DISPLAY_ICON)
            val artId = if (art == null) "" else Logic.artId(c.packageName, title, artist,
                md.getString(MediaMetadata.METADATA_KEY_ALBUM), art.width, art.height)

            val answer = withStand { it.media(title, artist, playing, position, duration, artId, ttlS = 300) } ?: return@withLock
            cleared = false
            if (answer == "need" && art != null) {
                val bytes = withContext(Dispatchers.Default) { artBytes(art) }
                if (withStand { it.mediaArt(artId, bytes) } != null) sentArt = artId
            }
            _state.update { it.copy(track = if (artist.isEmpty()) title else "$title · $artist", playing = playing) }
            if (playing && heartbeat?.isActive != true) {  // keeps the stand's position and timeout fresh
                heartbeat = scope?.launch { while (isActive) { delay(60_000); push() } }
            } else if (!playing) heartbeat?.cancel()
        }

        /** Centre square of the art, scaled to the stand's size, as RGB565. */
        private fun artBytes(src: Bitmap): ByteArray {
            val side = minOf(src.width, src.height)
            val square = Bitmap.createBitmap(src, (src.width - side) / 2, (src.height - side) / 2, side, side)
            val small = square.scale(Logic.ART_SIZE, Logic.ART_SIZE, filter = true)
            val px = IntArray(Logic.ART_SIZE * Logic.ART_SIZE)
            small.copy(Bitmap.Config.ARGB_8888, false).getPixels(px, 0, Logic.ART_SIZE, 0, 0, Logic.ART_SIZE, Logic.ART_SIZE)
            return Logic.rgb565(px)
        }
    }
}
