package app.w6.companion

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withTimeoutOrNull
import java.net.Inet4Address
import kotlin.coroutines.resume

/**
 * Finds stands on the Wi-Fi with mDNS (DNS-SD). The stand announces an HTTP service named "w6" (net.cpp). Android
 * doesn't resolve "w6.local" by name everywhere, so the app stores the IP address and looks the name up again when
 * the address stops answering.
 */
class Discovery(context: Context) {
    data class Found(val name: String, val host: String)

    private val nsd = context.getSystemService(NsdManager::class.java)

    /** Stands as they are found, for as long as the flow is collected. */
    fun stands(): Flow<List<Found>> = callbackFlow {
        val found = linkedMapOf<String, Found>()
        val toResolve = Channel<NsdServiceInfo>(Channel.UNLIMITED)
        launch {  // older Android resolves one service at a time
            for (info in toResolve) {
                val host = resolve(info) ?: continue
                found[info.serviceName] = Found(info.serviceName, host)
                trySend(found.values.toList())
            }
        }
        val listener = object : NsdManager.DiscoveryListener {
            override fun onServiceFound(info: NsdServiceInfo) {
                if (info.serviceName.startsWith("w6", ignoreCase = true)) toResolve.trySend(info)
            }
            override fun onServiceLost(info: NsdServiceInfo) {
                if (found.remove(info.serviceName) != null) trySend(found.values.toList())
            }
            override fun onDiscoveryStarted(serviceType: String) { trySend(found.values.toList()) }
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { close() }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }
        nsd.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
        awaitClose {
            toResolve.close()
            runCatching { nsd.stopServiceDiscovery(listener) }
        }
    }

    /** The current address of the stand announced as [name], or null if it doesn't show up within [timeoutMs]. */
    suspend fun find(name: String, timeoutMs: Long = 6000): String? = withTimeoutOrNull(timeoutMs) {
        stands().first { list -> list.any { it.name.equals(name, ignoreCase = true) } }
            .first { it.name.equals(name, ignoreCase = true) }.host
    }

    @Suppress("DEPRECATION")  // registerServiceInfoCallback (API 34) does the same; resolveService works on every version
    private suspend fun resolve(info: NsdServiceInfo): String? = suspendCancellableCoroutine { cont ->
        nsd.resolveService(info, object : NsdManager.ResolveListener {
            override fun onResolveFailed(i: NsdServiceInfo, errorCode: Int) { if (cont.isActive) cont.resume(null) }
            override fun onServiceResolved(i: NsdServiceInfo) {
                val addr = if (Build.VERSION.SDK_INT >= 34) i.hostAddresses.firstOrNull { it is Inet4Address } ?: i.hostAddresses.firstOrNull()
                else i.host
                val host = addr?.hostAddress?.let { if (':' in it) "[$it]" else it }
                val port = if (i.port == 80 || i.port == 0) "" else ":${i.port}"
                if (cont.isActive) cont.resume(host?.plus(port))
            }
        })
    }
}
