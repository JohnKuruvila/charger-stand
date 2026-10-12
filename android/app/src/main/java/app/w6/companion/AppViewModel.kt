package app.w6.companion

import android.app.Application
import android.graphics.Bitmap
import android.net.Uri
import android.provider.OpenableColumns
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.receiveAsFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

/** Everything the screens show and do. The stand is polled only while a screen is visible ([visible]). */
class AppViewModel(app: Application) : AndroidViewModel(app) {
    private val ctx get() = getApplication<Application>()

    data class Ui(
        val paired: Boolean,
        val onboarded: Boolean,
        val listenerAllowed: Boolean,
        val status: StandStatus? = null,
        val screen: ImageBitmap? = null,
        val reachable: Boolean? = null,  // null: not asked yet
        val error: String? = null,
        val host: String = "",
        val shares: Map<Prefs.Share, Boolean> = emptyMap(),
        val timerLeftS: Long? = null,    // counted down locally between polls
        val log: StandLog? = null,
        val ota: Ota = Ota(),
        val refreshing: Boolean = false,  // pull to refresh
    )

    data class Ota(
        val uri: Uri? = null, val name: String = "", val size: Long = 0, val valid: Boolean = false,
        val progress: Float? = null, val done: Boolean = false, val error: String? = null,
    )

    private val _ui = MutableStateFlow(fresh())
    val ui: StateFlow<Ui> = _ui

    private val _messages = Channel<String>(Channel.BUFFERED)
    val messages = _messages.receiveAsFlow()

    private fun fresh() = Ui(
        paired = Prefs.paired(ctx), onboarded = Prefs.onboarded(ctx), listenerAllowed = Link.listenerAllowed(ctx),
        host = Prefs.host(ctx), shares = Prefs.Share.entries.associateWith { Prefs.get(ctx, it) },
    )

    private fun api(): StandApi = StandApi(Prefs.host(ctx), Prefs.token(ctx), Link.network()?.socketFactory)

    private fun say(text: String) { _messages.trySend(text) }

    /**
     * Runs a call to the stand from a button; failures become a snackbar. [shown] updates the status at once on
     * success (the face picked, a switch), so the screen doesn't wait for the next poll to show it.
     */
    private fun act(done: String? = null, shown: ((StandStatus) -> StandStatus)? = null, block: suspend (StandApi) -> Unit) =
        viewModelScope.launch {
            try {
                block(api())
                if (shown != null) _ui.update { u -> u.copy(status = u.status?.let(shown)) }
                done?.let(::say)
                poll(screenshot = true, force = true)
            } catch (e: StandException) {
                say(Link.describe(e))
            }
        }

    // ---------------------------------------------------------------- polling

    private var poller: Job? = null
    private var ticker: Job? = null
    @Volatile private var screenWanted = false

    /** Whether a screen showing the stand's picture is open (Home). Elsewhere only the status is polled. */
    fun watchScreen(on: Boolean) {
        val was = screenWanted
        screenWanted = on
        if (on && !was) viewModelScope.launch { poll(screenshot = true, force = true) }
    }

    /** Pull to refresh: the status and the picture now. */
    fun refresh() = viewModelScope.launch {
        _ui.update { it.copy(refreshing = true) }
        poll(screenshot = true, force = true)
        _ui.update { it.copy(refreshing = false) }
    }

    fun visible(on: Boolean) {
        poller?.cancel(); ticker?.cancel()
        if (!on) return
        _ui.update { it.copy(listenerAllowed = Link.listenerAllowed(ctx), paired = Prefs.paired(ctx), host = Prefs.host(ctx)) }
        poller = viewModelScope.launch {
            while (isActive) {
                if (Prefs.paired(ctx)) poll(screenshot = true)
                delay(2500)
            }
        }
        ticker = viewModelScope.launch {  // the timer counts down on screen between polls
            while (isActive) {
                delay(1000)
                _ui.update { u -> u.timerLeftS?.let { u.copy(timerLeftS = (it - 1).coerceAtLeast(0)) } ?: u }
            }
        }
    }

    private var lastShotBytes = 0
    private var lastShotAt = 0L
    private val polling = Mutex()  // one poll at a time: two pictures at once would stall the stand twice

    /**
     * Reads the status and, while a screen shows it, the stand's picture. The screen stands still while the stand sends
     * its picture: ~0.2 s for a still face, about a second for a living face (every pixel moving barely compresses), or
     * 1-2 s on firmware without the small format. So the picture is refreshed every 5 s, every 10 s after a big one,
     * and at once when something changed ([force]: an action, pull to refresh, coming back to the screen).
     */
    private suspend fun poll(screenshot: Boolean, force: Boolean = false): Unit = polling.withLock {
        try {
            val a = api()
            val s = a.status()
            _ui.update { it.copy(status = s, reachable = true, error = null, timerLeftS = s.timerLeftS) }
            syncTimeZone(a, s)
            val now = System.currentTimeMillis()
            val every = if (lastShotBytes >= BIG_SHOT || !s.has("screenshot_rle")) 10_000 else 5_000
            if (screenshot && screenWanted && (force || now - lastShotAt >= every)) {
                val small = s.has("screenshot_rle")
                val bytes = a.screenshot(small)
                lastShotBytes = bytes.size
                lastShotAt = now
                val px = withContext(Dispatchers.Default) { Logic.decodeScreenshot(bytes) }
                if (px != null) {
                    val bmp = Bitmap.createBitmap(px, Logic.SCREEN, Logic.SCREEN, Bitmap.Config.ARGB_8888)
                    _ui.update { it.copy(screen = bmp.asImageBitmap()) }
                }
            }
        } catch (e: StandException) {
            _ui.update { it.copy(reachable = false, error = Link.describe(e)) }
            if (e is StandException.Unreachable) Link.findAgain()?.let { h -> _ui.update { it.copy(host = h) } }
        }
    }

    /**
     * The stand's clock shows the phone's time zone: sent whenever the stand's differs, e.g. after a trip or a
     * daylight-saving change. Firmware without the setting ("utc_offset" in features) is left alone.
     */
    private suspend fun syncTimeZone(a: StandApi, s: StandStatus) {
        val phone = Logic.utcOffsetSeconds()
        if (!s.has("utc_offset") || s.utcOffsetS == phone) return
        a.utcOffset(phone)
        _ui.update { it.copy(status = s.copy(utcOffsetS = phone)) }
    }

    private companion object {
        const val BIG_SHOT = 40_000  // bytes: a run-length-coded picture this big is a living face
    }

    // ---------------------------------------------------------------- pairing

    /** Checks the address and token against the stand; on success the phone is paired. Returns an error, or null. */
    suspend fun pair(hostInput: String, token: String, serviceName: String): String? {
        val host = Logic.normalizeHost(hostInput) ?: return "That doesn't look like an address. Try w6.local or 192.168.0.50."
        if (token.isBlank()) return "Enter the token from the setup page."
        return try {
            val api = StandApi(host, token.trim(), Link.network()?.socketFactory)
            try {
                api.status()
            } catch (e: StandException.Unreachable) {
                delay(1500)  // one more try: the stand may just have been busy
                api.status()
            }
            Prefs.pair(ctx, host, token, serviceName)
            _ui.update { it.copy(paired = true, host = host, status = null, screen = null, reachable = null) }
            Link.refresh()
            null
        } catch (e: StandException.Unauthorized) {
            "The stand didn't accept that token. Check it on the setup page."
        } catch (e: StandException.Unreachable) {
            if (e.slow) "The stand at $host is slow to answer right now. Wait a minute and try again; if it keeps " +
                "happening, unplug the stand for a few seconds."
            else "Nothing answered at $host. Is the phone on the same Wi-Fi as the stand?"
        } catch (e: StandException) {
            Link.describe(e)
        }
    }

    /** Asks the stand at [hostInput] to show a pairing code. Returns an error, or null when the code is up. */
    suspend fun pairStart(hostInput: String): PairStart {
        val host = Logic.normalizeHost(hostInput) ?: return PairStart.Error("That doesn't look like an address. Try w6.local or 192.168.0.50.")
        val api = StandApi(host, "", Link.network()?.socketFactory)
        return try {
            PairStart.Showing(host, api.pairStart())
        } catch (e: StandException.Http) {
            when (e.code) {
                404 -> PairStart.OldFirmware(host)
                429 -> PairStart.Showing(host, 120)  // a code from a moment ago is still on the screen
                else -> PairStart.Error(e.message ?: "The stand said no")
            }
        } catch (e: StandException.Unreachable) {
            PairStart.Error(if (e.slow) "The stand at $host is slow to answer. Try again in a moment."
                else "Nothing answered at $host. Is the phone on the same Wi-Fi as the stand?")
        } catch (e: StandException) {
            PairStart.Error(Link.describe(e))
        }
    }

    sealed interface PairStart {
        data class Showing(val host: String, val seconds: Int) : PairStart
        data class OldFirmware(val host: String) : PairStart
        data class Error(val message: String) : PairStart
    }

    /** Sends the code from the stand's screen; on success the phone is paired. Returns an error, or null. */
    suspend fun pairWithCode(host: String, codeInput: String, serviceName: String): String? {
        val code = Logic.pairCode(codeInput) ?: return "The code is 8 letters and digits, as shown on the stand."
        return try {
            val token = StandApi(host, "", Link.network()?.socketFactory).pairFinish(code)
            Prefs.pair(ctx, host, token, serviceName)
            _ui.update { it.copy(paired = true, host = host, status = null, screen = null, reachable = null) }
            Link.refresh()
            null
        } catch (e: StandException.Http) {
            when (e.code) {
                403 -> if ("too many" in (e.message ?: "")) "Too many wrong codes. Start again." else "That code didn't match. Check it on the stand."
                410 -> "The code on the stand has expired. Start again."
                else -> e.message
            }
        } catch (e: StandException) {
            Link.describe(e)
        }
    }

    /** A w6://pair link opened from outside the app, held until the user confirms it on the pairing screen. */
    private val _incoming = MutableStateFlow<Logic.PairLink?>(null)
    val incoming: StateFlow<Logic.PairLink?> = _incoming
    fun offerLink(text: String?) {
        val link = text?.let(Logic::pairLink) ?: return
        if (Prefs.paired(ctx)) say("This phone is already paired. Forget the stand in its settings first.")
        else _incoming.value = link
    }
    fun clearIncoming() { _incoming.value = null }

    /** Forgets the stand at once; taking the phone's charge and music off its screen is tried for a few seconds after. */
    fun unpair() {
        val a = api()
        Prefs.unpair(ctx)
        _ui.value = fresh()
        viewModelScope.launch {
            withTimeoutOrNull(6000) { runCatching { a.clearPhone() }; runCatching { a.clearMedia() } }
        }
    }

    fun finishOnboarding() {
        Prefs.setOnboarded(ctx)
        _ui.update { it.copy(onboarded = true) }
    }

    fun recheckListener() = _ui.update { it.copy(listenerAllowed = Link.listenerAllowed(ctx)) }

    // ---------------------------------------------------------------- what the phone shares

    fun setShare(s: Prefs.Share, on: Boolean) {
        Prefs.set(ctx, s, on)
        _ui.update { it.copy(shares = it.shares + (s to on)) }
        Link.refresh()
    }

    // ---------------------------------------------------------------- the stand

    fun face(name: String) = act(shown = { it.copy(face = name) }) { it.face(name) }
    fun chargingStyle(name: String) = act(shown = { it.copy(chargingStyle = name) }) { it.chargingStyle(name) }
    fun timer(minutes: Int, label: String) = act("Timer started on the stand") { it.timer(minutes, label) }
    fun cancelTimer() = act("Timer stopped") { it.cancelTimer() }
    fun rotation(q: Int) = act(shown = { it.copy(rotation = q) }) { it.rotation(q) }
    fun clock24h(on: Boolean) = act(shown = { it.copy(clock24h = on) }) { it.clock24h(on) }

    /** Settings → Stand address: search the network for the stand now. */
    fun findAgain() = viewModelScope.launch {
        say("Looking for the stand…")
        val host = Link.findAgain(force = true)
        if (host != null) _ui.update { it.copy(host = host) }
        say(if (host != null) "Found the stand at $host" else "No stand answered on this Wi-Fi")
        poll(screenshot = true, force = true)
    }
    fun reboot() = act("The stand is restarting") { it.reboot() }

    fun loadLog() = viewModelScope.launch {
        try {
            _ui.update { it.copy(log = api().log()) }
        } catch (e: StandException) {
            say(Link.describe(e))
        }
    }

    // ---------------------------------------------------------------- firmware

    fun pickFirmware(uri: Uri) = viewModelScope.launch {
        val cr = ctx.contentResolver
        var name = "firmware.bin"
        var size = 0L
        cr.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
            if (c.moveToFirst()) { name = c.getString(0) ?: name; size = c.getLong(1) }
        }
        // An ESP32 app image starts with 0xE9; anything else would be refused by the stand anyway
        val magic = withContext(Dispatchers.IO) { runCatching { cr.openInputStream(uri)?.use { it.read() } }.getOrNull() }
        val ok = magic == 0xE9 && size in 100_000..1_966_080
        _ui.update { it.copy(ota = Ota(uri, name, size, ok, error = if (ok) null else "This isn't a W6 firmware file (firmware.bin from the firmware build).")) }
    }

    fun installFirmware() {
        val o = _ui.value.ota
        val uri = o.uri ?: return
        viewModelScope.launch {
            _ui.update { it.copy(ota = o.copy(progress = 0f, error = null, done = false)) }
            try {
                api().ota({ ctx.contentResolver.openInputStream(uri)!! }, o.size, o.name) { p ->
                    _ui.update { it.copy(ota = it.ota.copy(progress = p)) }
                }
                _ui.update { it.copy(ota = it.ota.copy(progress = 1f, done = true)) }
            } catch (e: StandException) {
                _ui.update { it.copy(ota = it.ota.copy(progress = null, error = Link.describe(e))) }
            }
        }
    }

    fun clearFirmware() = _ui.update { it.copy(ota = Ota()) }
}
