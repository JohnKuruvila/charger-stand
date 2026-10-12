package app.w6.companion

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import okhttp3.Call
import okhttp3.Callback
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.MultipartBody
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody
import okhttp3.RequestBody.Companion.toRequestBody
import okhttp3.Response
import okio.BufferedSink
import okio.source
import org.json.JSONObject
import java.io.IOException
import java.io.InputStream
import java.util.concurrent.TimeUnit
import javax.net.SocketFactory
import kotlin.coroutines.resume
import kotlin.coroutines.resumeWithException

/** Why a call to the stand failed, in terms the screens can show. */
sealed class StandException(message: String) : Exception(message) {
    /** No answer: the stand is off, on another network, or its address changed. */
    class Unreachable(cause: IOException) : StandException(cause.message ?: "unreachable") {
        /** It took the connection, or started to, but didn't answer in time: busy, or a weak Wi-Fi link. */
        val slow = cause is java.net.SocketTimeoutException || cause is java.io.InterruptedIOException
    }
    class Unauthorized : StandException("wrong token")
    class Http(val code: Int, message: String) : StandException(message)
}

/**
 * The stand's HTTP API (firmware/w6/README.md), one call per function. Calls are cancelled with their coroutine.
 * [socketFactory] pins the calls to the Wi-Fi network, so they don't go out over mobile data when the home Wi-Fi has
 * no internet and Android prefers mobile data.
 */
class StandApi(val host: String, private val token: String, socketFactory: SocketFactory? = null) {

    private val client: OkHttpClient = base.newBuilder().apply { if (socketFactory != null) socketFactory(socketFactory) }.build()
    private val url = "http://$host"

    /**
     * Pairing needs no token: the stand shows a one-time code for two minutes (firmware 0.3.2+). Returns the seconds
     * the code stays up. A 404 means older firmware, which pairs only with the token.
     */
    suspend fun pairStart(): Int = json(post("/api/pair", JSONObject())).optInt("expires_s", 120)

    /** Trades the code from the stand's screen for its API token. */
    suspend fun pairFinish(code: String): String =
        json(post("/api/pair/finish", JSONObject().put("code", code))).optString("token").ifEmpty {
            throw StandException.Http(500, "The stand didn't send a token")
        }

    suspend fun status(): StandStatus = StandStatus.parse(json(get("/api/status")))
    suspend fun log(): StandLog = StandLog.parse(json(get("/api/log")))
    /** The stand's screen. [small]: run-length coded (firmware 0.3.8+), about 14 KB instead of 115 KB and sent in a
     *  quarter of the time; the stand's screen stands still while either is sent. */
    suspend fun screenshot(small: Boolean = false): ByteArray =
        bytes(get(if (small) "/api/screenshot?format=rle" else "/api/screenshot"))

    suspend fun phone(r: Logic.PhoneReport, ttlS: Int = Logic.PHONE_TTL_S) {
        val o = JSONObject().put("percent", r.percent).put("charging", r.charging).put("ttl", ttlS)
        r.watts?.let { o.put("watts", it) }
        send(post("/api/phone", o))
    }

    suspend fun clearPhone() = send(delete("/api/phone"))

    /** Returns the stand's answer about the art: "need" (send it with [mediaArt]), "have" or "none". */
    suspend fun media(title: String, artist: String, playing: Boolean, positionMs: Long, durationMs: Long, artId: String, ttlS: Int): String {
        val o = JSONObject().put("title", title).put("artist", artist).put("playing", playing).put("ttl", ttlS).put("art_id", artId)
        if (positionMs >= 0) o.put("position_ms", positionMs)
        if (durationMs > 0) o.put("duration_ms", durationMs)
        return json(post("/api/media", o)).optString("art", "none")
    }

    suspend fun mediaArt(id: String, rgb565: ByteArray) {
        val body = MultipartBody.Builder().setType(MultipartBody.FORM)
            .addFormDataPart("art", "art.bin", rgb565.toRequestBody(OCTET))
            .build()
        send(request("/api/media/art?id=$id").post(body).build())
    }

    suspend fun clearMedia() = send(delete("/api/media"))


    suspend fun timer(minutes: Int, label: String) = send(post("/api/timer", JSONObject().put("minutes", minutes).put("label", label)))
    suspend fun cancelTimer() = send(delete("/api/timer"))
    suspend fun face(name: String) = send(post("/api/settings", JSONObject().put("face", name)))
    suspend fun chargingStyle(name: String) = send(post("/api/settings", JSONObject().put("charging_style", name)))
    suspend fun rotation(quarterTurns: Int) = send(post("/api/settings", JSONObject().put("rotation", quarterTurns)))
    suspend fun clock24h(on: Boolean) = send(post("/api/settings", JSONObject().put("clock_24h", on)))
    suspend fun utcOffset(seconds: Int) = send(post("/api/settings", JSONObject().put("utc_offset_s", seconds)))
    suspend fun reboot() = send(post("/api/reboot", JSONObject()))

    /**
     * Installs firmware. [progress] gets 0..1 as the file goes out; the stand restarts into the new version when the
     * call returns, and goes back to the old one by itself if the new one can't get online (README "If an update breaks it").
     */
    suspend fun ota(open: () -> InputStream, size: Long, name: String, progress: (Float) -> Unit) {
        val file = object : RequestBody() {
            override fun contentType() = OCTET
            override fun contentLength() = size
            override fun writeTo(sink: BufferedSink) {
                open().source().use { src ->
                    var sent = 0L
                    while (true) {
                        val n = src.read(sink.buffer, 16 * 1024)
                        if (n < 0) break
                        sink.flush()
                        sent += n
                        progress((sent.toFloat() / size).coerceAtMost(1f))
                    }
                }
            }
        }
        val body = MultipartBody.Builder().setType(MultipartBody.FORM).addFormDataPart("firmware", name, file).build()
        send(request("/api/ota").post(body).build(), slow = true)
    }

    // ---------------------------------------------------------------- plumbing

    private fun request(path: String) = Request.Builder().url(url + path)
        .apply { if (token.isNotEmpty()) header("Authorization", "Bearer $token") }
    private fun get(path: String) = request(path).get().build()
    private fun delete(path: String) = request(path).delete().build()
    private fun post(path: String, o: JSONObject) = request(path).post(o.toString().toRequestBody(JSON)).build()

    private suspend fun send(r: Request, slow: Boolean = false) = exchange(r, slow) { }
    private suspend fun json(r: Request, slow: Boolean = false): JSONObject = exchange(r, slow) { JSONObject(it.body.string()) }
    private suspend fun bytes(r: Request): ByteArray = exchange(r, false) { it.body.bytes() }

    /**
     * Makes the call and reads the reply off the main thread: OkHttp hands over the response once the headers are in,
     * and reading the body is network I/O, which Android refuses on the main thread (it crashed 0.1.0).
     */
    private suspend fun <T> exchange(r: Request, slow: Boolean, read: (Response) -> T): T = withContext(Dispatchers.IO) {
        val resp = call(r, slow)
        try {
            resp.use(read)
        } catch (e: IOException) {
            throw StandException.Unreachable(e)
        } catch (e: org.json.JSONException) {
            throw StandException.Http(resp.code, "The stand sent a reply W6 doesn't understand")
        }
    }

    private suspend fun call(r: Request, slow: Boolean): Response {
        val c = if (slow) client.newBuilder().readTimeout(60, TimeUnit.SECONDS).writeTimeout(120, TimeUnit.SECONDS).build() else client
        val resp = try {
            c.newCall(r).await()
        } catch (e: IOException) {
            throw StandException.Unreachable(e)
        }
        if (resp.isSuccessful) return resp
        val msg = resp.use { runCatching { JSONObject(it.body.string()).optString("error") }.getOrNull() }
        if (resp.code == 401) throw StandException.Unauthorized()
        throw StandException.Http(resp.code, msg?.takeIf { it.isNotBlank() } ?: "HTTP ${resp.code}")
    }

    companion object {
        private val JSON = "application/json".toMediaType()
        private val OCTET = "application/octet-stream".toMediaType()

        // The stand usually answers in well under a second, but a request can wait several seconds while it is busy
        // (a screenshot, Wi-Fi trouble), so these are generous rather than quick.
        private val base = OkHttpClient.Builder()
            .connectTimeout(8, TimeUnit.SECONDS)
            .readTimeout(15, TimeUnit.SECONDS)
            .writeTimeout(10, TimeUnit.SECONDS)
            .retryOnConnectionFailure(true)
            .build()
    }
}

private suspend fun Call.await(): Response = suspendCancellableCoroutine { cont ->
    enqueue(object : Callback {
        override fun onResponse(call: Call, response: Response) = cont.resume(response)
        override fun onFailure(call: Call, e: IOException) { if (!cont.isCancelled) cont.resumeWithException(e) }
    })
    cont.invokeOnCancellation { cancel() }
}
