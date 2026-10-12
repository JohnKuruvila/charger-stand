package app.w6.companion

import java.security.MessageDigest
import java.text.Normalizer
import java.time.Instant
import java.time.ZoneId

/**
 * Pure decisions and conversions, free of Android types so they run in plain unit tests (LogicTest).
 */
object Logic {

    // ---------------------------------------------------------------- battery

    /** What the stand's charging screen needs. [watts] is null when the phone doesn't report a believable value. */
    data class PhoneReport(val percent: Int, val charging: Boolean, val watts: Double?)

    /**
     * BatteryManager.BATTERY_PROPERTY_CURRENT_NOW in microamps. Android says µA, but some phones report mA, and
     * Int.MIN_VALUE or 0 mean "not supported". Readings under 10 000 are taken as mA: no phone charges at under 10 mA.
     */
    fun microAmps(raw: Int): Long? = when {
        raw == Int.MIN_VALUE || raw == 0 -> null
        kotlin.math.abs(raw) < 10_000 -> raw * 1000L
        else -> raw.toLong()
    }

    /**
     * Power going into the battery, in watts: current × voltage, as the phone measures it. It is net of what the phone
     * itself is using, so a busy phone on a weak charger can be charging and still show nothing.
     *
     * Phones disagree on the sign of the current. [dischargeSign] is the sign seen while the phone ran on battery
     * (learned with [learnDischargeSign]); 0 when not known yet, and then the size of the current is used as is.
     * Returns null for no reading, for a phone that is losing charge on the charger, and for impossible values.
     */
    fun chargeWatts(currentUa: Long?, voltageMv: Int?, dischargeSign: Int): Double? {
        if (currentUa == null || voltageMv == null || voltageMv < 2500 || voltageMv > 5000) return null
        val intoBattery = when {
            dischargeSign > 0 -> -currentUa
            dischargeSign < 0 -> currentUa
            else -> kotlin.math.abs(currentUa)
        }
        if (intoBattery <= 0) return null
        val w = intoBattery / 1e6 * voltageMv / 1e3
        return if (w > 60) null else Math.round(w * 10) / 10.0
    }

    /** The sign of the current while not charging and drawing a clear amount; null when this reading can't tell. */
    fun learnDischargeSign(currentUa: Long?, pluggedIn: Boolean): Int? {
        if (pluggedIn || currentUa == null || kotlin.math.abs(currentUa) < 50_000) return null
        return if (currentUa > 0) 1 else -1
    }

    const val PHONE_HEARTBEAT_MS = 20_000L
    const val PHONE_TTL_S = 120

    /**
     * Whether to send [next] now, given what was last sent and when. A change of percent or charging goes at once;
     * while charging, a heartbeat every [PHONE_HEARTBEAT_MS] keeps the stand's screen alive (it expires after
     * [PHONE_TTL_S]) and carries the latest watts. Once a "not charging" has been sent, nothing more is needed.
     */
    fun shouldSendPhone(last: PhoneReport?, lastAtMs: Long, next: PhoneReport, nowMs: Long): Boolean {
        if (last == null) return true
        if (last.percent != next.percent || last.charging != next.charging) return true
        return next.charging && nowMs - lastAtMs >= PHONE_HEARTBEAT_MS
    }

    // ---------------------------------------------------------------- text for the stand

    private val replacements = mapOf(
        '‘' to "'", '’' to "'", '‚' to "'", '‛' to "'", '“' to "\"", '”' to "\"", '„' to "\"",
        '—' to " - ", '‒' to "-", '−' to "-", '×' to "x", '•' to "·", 'ß' to "ss", 'Æ' to "AE", 'æ' to "ae",
        'Ø' to "O", 'ø' to "o", 'Œ' to "OE", 'œ' to "oe", 'Ł' to "L", 'ł' to "l", 'Đ' to "D", 'đ' to "d",
        ' ' to " ",
    )

    /** Characters the stand's fonts have: printable ASCII, °, ·, – and …. */
    private fun onStand(c: Char) = c in ' '..'~' || c == '°' || c == '·' || c == '–' || c == '…'

    /**
     * Fits text to the stand's fonts: accents are dropped (é → e), curly quotes straightened, and anything the stand
     * can't draw (other scripts, emoji) removed. Cut to [max] characters with an ellipsis.
     */
    fun standText(s: String, max: Int): String {
        val sb = StringBuilder()
        for (ch in s) {
            if (onStand(ch)) { sb.append(ch); continue }
            replacements[ch]?.let { sb.append(it); continue }
            val base = Normalizer.normalize(ch.toString(), Normalizer.Form.NFD).filter { onStand(it) }
            if (base.isNotEmpty()) sb.append(base)
        }
        val out = sb.toString().replace(Regex("\\s+"), " ").trim()
        return if (out.length <= max) out else out.take(max - 1).trimEnd() + "…"
    }

    // ---------------------------------------------------------------- album art

    const val ART_SIZE = 112

    /** ARGB pixels (row by row) to the stand's art format: RGB565, high byte first. */
    fun rgb565(argb: IntArray): ByteArray {
        val out = ByteArray(argb.size * 2)
        for (i in argb.indices) {
            val p = argb[i]
            val r = (p shr 16) and 0xFF
            val g = (p shr 8) and 0xFF
            val b = p and 0xFF
            val v = ((r shr 3) shl 11) or ((g shr 2) shl 5) or (b shr 3)
            out[2 * i] = (v shr 8).toByte()
            out[2 * i + 1] = v.toByte()
        }
        return out
    }

    /** A short id for a track's art, so it is sent once per track, not with every update. */
    fun artId(vararg parts: Any?): String {
        val d = MessageDigest.getInstance("SHA-256").digest(parts.joinToString("\u0000").toByteArray())
        return d.take(6).joinToString("") { "%02x".format(it) }
    }

    /** Where playback is now, from Android's last reported position. */
    fun positionNow(positionMs: Long, updatedElapsedMs: Long, speed: Float, nowElapsedMs: Long, playing: Boolean): Long {
        if (positionMs < 0) return -1
        if (!playing || updatedElapsedMs <= 0) return positionMs
        return positionMs + ((nowElapsedMs - updatedElapsedMs) * speed).toLong()
    }

    // ---------------------------------------------------------------- screenshot

    const val SCREEN = 240

    /**
     * Decodes GET /api/screenshot: a 240 × 240 BMP, 16-bit RGB565 little-endian, top-down, pixel data at the offset in
     * its header. Returns ARGB pixels, or null for anything else.
     */
    fun decodeScreenshot(bmp: ByteArray): IntArray? {
        if (bmp.size >= 8 && String(bmp, 0, 4, Charsets.US_ASCII) == "W6R1") return decodeRle(bmp)
        fun u16(i: Int) = (bmp[i].toInt() and 0xFF) or ((bmp[i + 1].toInt() and 0xFF) shl 8)
        fun u32(i: Int) = u16(i) or (u16(i + 2) shl 16)
        if (bmp.size < 66 || bmp[0] != 'B'.code.toByte() || bmp[1] != 'M'.code.toByte()) return null
        val offset = u32(10)
        val w = u32(18)
        val h = u32(22)
        if (w != SCREEN || (h != -SCREEN && h != SCREEN) || u16(28) != 16) return null
        if (bmp.size < offset + SCREEN * SCREEN * 2) return null
        val out = IntArray(SCREEN * SCREEN)
        for (row in 0 until SCREEN) {
            val y = if (h < 0) row else SCREEN - 1 - row
            for (x in 0 until SCREEN) {
                val v = u16(offset + (row * SCREEN + x) * 2)
                val r = (v shr 11) and 31
                val g = (v shr 5) and 63
                val b = v and 31
                out[y * SCREEN + x] = (0xFF shl 24) or ((r * 255 / 31) shl 16) or ((g * 255 / 63) shl 8) or (b * 255 / 31)
            }
        }
        return out
    }

    private fun argb(v: Int) = (0xFF shl 24) or (((v shr 11) and 31) * 255 / 31 shl 16) or
        (((v shr 5) and 63) * 255 / 63 shl 8) or ((v and 31) * 255 / 31)

    /**
     * GET /api/screenshot?format=rle: "W6R1", width and height (16-bit little-endian), then packets of RGB565 pixels
     * (little-endian) in reading order: a byte h with the top bit set is one pixel repeated (h and 0x7F) + 1 times,
     * otherwise h + 1 pixels follow. Null for anything that doesn't fill exactly one 240 × 240 screen.
     */
    fun decodeRle(b: ByteArray): IntArray? {
        fun u16(i: Int) = (b[i].toInt() and 0xFF) or ((b[i + 1].toInt() and 0xFF) shl 8)
        if (b.size < 8 || u16(4) != SCREEN || u16(6) != SCREEN) return null
        val out = IntArray(SCREEN * SCREEN)
        var o = 0
        var i = 8
        while (i < b.size) {
            val h = b[i].toInt() and 0xFF
            i++
            val n = (h and 0x7F) + 1
            if (o + n > out.size) return null
            if (h and 0x80 != 0) {
                if (i + 2 > b.size) return null
                out.fill(argb(u16(i)), o, o + n)
                i += 2
            } else {
                if (i + 2 * n > b.size) return null
                for (k in 0 until n) out[o + k] = argb(u16(i + 2 * k))
                i += 2 * n
            }
            o += n
        }
        return if (o == out.size) out else null
    }

    // ---------------------------------------------------------------- addresses

    private val hostPattern = Regex("^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?(:[0-9]{1,5})?$")

    /**
     * The stand's address as typed: "w6.local", "192.168.0.50" or "http://192.168.0.50/" all give a bare host
     * (with a port if one was typed). Null when it can't be an address.
     */
    fun normalizeHost(input: String): String? {
        var s = input.trim()
        s = s.removePrefix("http://").removePrefix("HTTP://")
        if (s.startsWith("https://", ignoreCase = true)) return null  // the stand speaks plain HTTP on the home network
        s = s.substringBefore('/')
        if (s.isEmpty() || !hostPattern.matches(s)) return null
        return s
    }

    // ---------------------------------------------------------------- pairing

    /** What the stand's pairing QR code holds: w6://pair?host=192.168.0.50&code=k7m2p9xq */
    data class PairLink(val host: String, val code: String)

    private const val CODE_ALPHABET = "abcdefghjkmnpqrstuvwxyz23456789"  // settings.cpp random_string

    /** A typed or scanned code as the stand wants it: 8 characters of its alphabet, any case, spaces and dashes ignored. */
    fun pairCode(input: String): String? {
        val c = input.lowercase().filterNot { it == ' ' || it == '-' }
        return if (c.length == 8 && c.all { it in CODE_ALPHABET }) c else null
    }

    /**
     * Reads a pairing link. Only addresses on a home network are accepted (private IPv4 ranges, link-local, or a .local
     * name), so a link on a web page can't point the app at a server on the internet.
     */
    fun pairLink(text: String): PairLink? {
        val t = text.trim()
        if (!t.startsWith("w6://pair?", ignoreCase = true)) return null
        val params = t.substringAfter('?').split('&').mapNotNull {
            val k = it.substringBefore('=', "")
            if (k.isEmpty()) null else k.lowercase() to java.net.URLDecoder.decode(it.substringAfter('='), "UTF-8")
        }.toMap()
        val host = normalizeHost(params["host"] ?: return null) ?: return null
        val code = pairCode(params["code"] ?: return null) ?: return null
        return if (isLocal(host)) PairLink(host, code) else null
    }

    fun isLocal(host: String): Boolean {
        val h = host.substringBefore(':').lowercase()
        if (h.endsWith(".local")) return true
        val p = h.split('.').mapNotNull { it.toIntOrNull() }
        if (p.size != 4 || p.any { it !in 0..255 }) return false
        return p[0] == 10 || (p[0] == 172 && p[1] in 16..31) || (p[0] == 192 && p[1] == 168) || (p[0] == 169 && p[1] == 254)
    }

    // ---------------------------------------------------------------- time zone

    /** The phone's offset from UTC now, in seconds east, for the stand's clock (it has no time zone rules of its own). */
    fun utcOffsetSeconds(zone: ZoneId = ZoneId.systemDefault(), at: Instant = Instant.now()): Int =
        zone.rules.getOffset(at).totalSeconds

    // ---------------------------------------------------------------- wording

    fun duration(seconds: Long): String = when {
        seconds < 60 -> "${seconds}s"
        seconds < 3600 -> "${seconds / 60}m ${"%02d".format(seconds % 60)}s"
        seconds < 86400 -> "${seconds / 3600}h ${"%02d".format(seconds / 60 % 60)}m"
        else -> "${seconds / 86400}d ${seconds / 3600 % 24}h"
    }

    /** Timer remaining as the stand shows it: 24:59 or 1:02:03. */
    fun clockDuration(seconds: Long): String {
        val s = seconds.coerceAtLeast(0)
        return if (s >= 3600) "%d:%02d:%02d".format(s / 3600, s / 60 % 60, s % 60) else "%02d:%02d".format(s / 60, s % 60)
    }

    /** A run cut off by a power loss: the stand saves its uptime at 10 s, 60 s and then every 10 minutes. */
    fun lostPowerRan(seconds: Long): String = when {
        seconds < 10 -> "under 10 s"
        seconds < 60 -> "10–60 s"
        seconds < 600 -> "1–10 min"
        else -> "at least ${duration(seconds)}"
    }
}
