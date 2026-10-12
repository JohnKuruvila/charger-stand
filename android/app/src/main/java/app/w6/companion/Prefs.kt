package app.w6.companion

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import androidx.core.content.edit
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * The app's settings. The stand's API token is encrypted with an AES-GCM key kept in the Android Keystore, so a
 * copied preferences file holds no usable token; backup and device transfer are also off in the manifest.
 */
object Prefs {
    private const val FILE = "w6"
    private const val KEY_ALIAS = "w6_token_key"

    private fun sp(c: Context): SharedPreferences = c.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    // ---------------------------------------------------------------- pairing

    fun host(c: Context): String = sp(c).getString("host", "").orEmpty()
    fun setHost(c: Context, host: String) = sp(c).edit { putString("host", host) }

    /** The mDNS name the stand was found under, used to find it again when its IP address changes. */
    fun serviceName(c: Context): String = sp(c).getString("service", "").orEmpty()

    @Volatile private var cache: Pair<String, String>? = null

    fun token(c: Context): String {
        val sealed = sp(c).getString("token_enc", "").orEmpty()
        if (sealed.isEmpty()) return ""
        cache?.let { (s, plain) -> if (s == sealed) return plain }
        val plain = runCatching { decrypt(sealed) }.getOrDefault("")
        cache = sealed to plain
        return plain
    }

    fun paired(c: Context) = host(c).isNotEmpty() && token(c).isNotEmpty()

    fun pair(c: Context, host: String, token: String, serviceName: String) {
        val sealed = encrypt(token.trim())
        sp(c).edit(commit = true) {
            putString("host", host)
            putString("service", serviceName)
            putString("token_enc", sealed)
        }
        cache = sealed to token.trim()
    }

    fun unpair(c: Context) {
        sp(c).edit(commit = true) { remove("host"); remove("service"); remove("token_enc") }
        cache = null
    }

    // ---------------------------------------------------------------- what to show on the stand

    enum class Share(val key: String, val default: Boolean) {
        Battery("share_battery", true),
        WirelessOnly("wireless_only", true),
        Media("share_media", true),
    }

    fun get(c: Context, s: Share) = sp(c).getBoolean(s.key, s.default)
    fun set(c: Context, s: Share, on: Boolean) = sp(c).edit { putBoolean(s.key, on) }

    /** +1 or -1: the sign of BatteryManager's current while this phone runs on battery; 0 until seen (Logic.chargeWatts). */
    fun dischargeSign(c: Context) = sp(c).getInt("discharge_sign", 0)
    fun setDischargeSign(c: Context, sign: Int) = sp(c).edit { putInt("discharge_sign", sign) }

    fun onboarded(c: Context) = sp(c).getBoolean("onboarded", false)
    fun setOnboarded(c: Context) = sp(c).edit { putBoolean("onboarded", true) }

    // ---------------------------------------------------------------- token encryption

    private fun key(): SecretKey {
        val ks = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }
        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        gen.init(
            KeyGenParameterSpec.Builder(KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build()
        )
        return gen.generateKey()
    }

    private fun encrypt(plain: String): String {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key()) }
        val out = cipher.iv + cipher.doFinal(plain.toByteArray(Charsets.UTF_8))
        return Base64.encodeToString(out, Base64.NO_WRAP)
    }

    private fun decrypt(sealed: String): String {
        val all = Base64.decode(sealed, Base64.NO_WRAP)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, all, 0, 12))
        return String(cipher.doFinal(all, 12, all.size - 12), Charsets.UTF_8)
    }
}
