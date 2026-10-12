package app.w6.companion

import org.json.JSONObject

/** GET /api/status. Fields the stand's firmware didn't send are null (older firmware sends fewer). */
data class StandStatus(
    val version: String,
    val uptimeS: Long,
    val resetReason: String?,
    val rssiDbm: Int?,
    val freeHeap: Long?,
    val rotation: Int?,
    val face: String?,
    val chipTempC: Double?,
    val features: Set<String>,
    val timerLeftS: Long?,
    val clock24h: Boolean? = null,  // null: firmware before 0.3.7, which always shows 24-hour time
    val utcOffsetS: Int? = null,  // the clock's time zone; null: not set, or firmware that took it from the weather
    val faces: List<String>? = null,  // every face the firmware has; null: firmware before 0.4.0 (classic, sun, analog)
    val chargingStyle: String? = null,  // how the charging screen looks; null: firmware before 0.6.3 (always classic)
    val chargingStyles: List<String>? = null,
) {
    fun has(feature: String) = feature in features

    companion object {
        fun parse(o: JSONObject): StandStatus {
            val f = o.optJSONArray("features")
            return StandStatus(
                version = o.optString("version", "?"),
                uptimeS = o.optLong("uptime_s"),
                resetReason = o.optStringOrNull("reset_reason"),
                rssiDbm = o.optIntOrNull("rssi_dbm"),
                freeHeap = o.optLongOrNull("free_heap"),
                rotation = o.optIntOrNull("rotation"),
                face = o.optStringOrNull("face"),
                chipTempC = o.optDoubleOrNull("chip_temp_c"),
                features = buildSet { if (f != null) for (i in 0 until f.length()) add(f.optString(i)) },
                timerLeftS = o.optLongOrNull("timer_left_s"),
                clock24h = o.optBooleanOrNull("clock_24h"),
                utcOffsetS = o.optIntOrNull("utc_offset_s"),
                faces = o.optJSONArray("faces")?.let { a -> List(a.length()) { a.optString(it) }.filter { it.isNotEmpty() } },
                chargingStyle = o.optStringOrNull("charging_style"),
                chargingStyles = o.optJSONArray("charging_styles")?.let { a -> List(a.length()) { a.optString(it) }.filter { it.isNotEmpty() } },
            )
        }
    }
}

/** GET /api/log: the stand's last starts, newest first, and this start's Wi-Fi events. */
data class StandLog(val boots: List<Boot>, val wifi: List<WifiEvent>, val wifiTotal: Int) {
    data class Boot(
        val n: Long,
        val startedUnix: Long?,
        val reason: String,
        val ranS: Long,
        val ranExact: Boolean,
        val running: Boolean,
        val gotOnline: Boolean,
        val setupByPlugging: Boolean,
        val wifiDrops: Int,
        val maxChipC: Int?,
        val minFreeHeap: Long?,
        val restartedFor: String?,
    )

    data class WifiEvent(val uptimeS: Long, val code: Int, val reason: String, val whileJoining: Boolean)

    companion object {
        fun parse(o: JSONObject): StandLog {
            val b = o.optJSONArray("boots")
            val boots = (0 until (b?.length() ?: 0)).map { i ->
                val e = b!!.getJSONObject(i)
                Boot(
                    n = e.optLong("n"),
                    startedUnix = e.optLongOrNull("started"),
                    reason = e.optString("reason", "other"),
                    ranS = e.optLong("ran_s"),
                    ranExact = e.optBoolean("ran_exact", true),
                    running = e.optBoolean("running"),
                    gotOnline = e.optBoolean("got_online"),
                    setupByPlugging = e.optBoolean("setup_by_plugging"),
                    wifiDrops = e.optInt("wifi_drops"),
                    maxChipC = e.optIntOrNull("max_chip_c"),
                    minFreeHeap = e.optLongOrNull("min_free_heap"),
                    restartedFor = e.optStringOrNull("restarted_for"),
                )
            }
            val w = o.optJSONArray("wifi_drops")
            val wifi = (0 until (w?.length() ?: 0)).map { i ->
                val e = w!!.getJSONObject(i)
                WifiEvent(e.optLong("uptime_s"), e.optInt("code"), e.optString("reason"), e.optBoolean("while_joining"))
            }
            return StandLog(boots, wifi, o.optInt("wifi_drops_total", wifi.size))
        }
    }
}

internal fun JSONObject.optStringOrNull(k: String): String? = if (has(k) && !isNull(k)) optString(k) else null
internal fun JSONObject.optIntOrNull(k: String): Int? = if (has(k) && !isNull(k)) optInt(k) else null
internal fun JSONObject.optLongOrNull(k: String): Long? = if (has(k) && !isNull(k)) optLong(k) else null
internal fun JSONObject.optBooleanOrNull(k: String): Boolean? = if (has(k) && !isNull(k)) optBoolean(k) else null
internal fun JSONObject.optDoubleOrNull(k: String): Double? = if (has(k) && !isNull(k)) optDouble(k) else null
