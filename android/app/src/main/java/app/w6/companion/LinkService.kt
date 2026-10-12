package app.w6.companion

import android.content.ComponentName
import android.service.notification.NotificationListenerService

/**
 * Android keeps a notification listener running once the user has allowed it, which is what lets W6 update the stand
 * with the app closed. It is also the permission Android asks for before an app may see what is playing.
 * W6 ignores the notifications themselves: nothing here reads, stores or sends them.
 */
class LinkService : NotificationListenerService() {
    override fun onListenerConnected() = Link.hold(this, "service")

    override fun onListenerDisconnected() {
        Link.release("service")
        // Android sometimes unbinds a listener (after an update, under memory pressure); ask to be bound again.
        if (Link.listenerAllowed(this)) requestRebind(ComponentName(this, LinkService::class.java))
    }
}
