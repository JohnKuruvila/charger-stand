package app.w6.companion

import org.json.JSONObject
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class LogicTest {

    @Test fun microAmpsTreatsSmallReadingsAsMilliamps() {
        assertNull(Logic.microAmps(Int.MIN_VALUE))
        assertNull(Logic.microAmps(0))
        assertEquals(1_200_000L, Logic.microAmps(1200))
        assertEquals(-850_000L, Logic.microAmps(-850_000))
    }

    @Test fun chargeWattsUsesTheLearnedSign() {
        // a Pixel-like phone: negative while discharging, so positive is charging
        assertEquals(4.0, Logic.chargeWatts(1_000_000, 4000, dischargeSign = -1)!!, 0.001)
        assertNull(Logic.chargeWatts(-1_000_000, 4000, dischargeSign = -1))  // losing charge on the charger
        // a phone that reports discharge as positive
        assertEquals(4.0, Logic.chargeWatts(-1_000_000, 4000, dischargeSign = 1)!!, 0.001)
        // not learned yet: the size is used
        assertEquals(2.2, Logic.chargeWatts(-550_000, 4000, dischargeSign = 0)!!, 0.001)
    }

    @Test fun chargeWattsRejectsNonsense() {
        assertNull(Logic.chargeWatts(null, 4000, -1))
        assertNull(Logic.chargeWatts(1_000_000, null, -1))
        assertNull(Logic.chargeWatts(1_000_000, 900, -1))          // voltage in volts by mistake
        assertNull(Logic.chargeWatts(20_000_000, 4400, -1))        // 88 W
    }

    @Test fun dischargeSignNeedsAClearReadingOffTheCharger() {
        assertEquals(-1, Logic.learnDischargeSign(-400_000, pluggedIn = false))
        assertEquals(1, Logic.learnDischargeSign(400_000, pluggedIn = false))
        assertNull(Logic.learnDischargeSign(-400_000, pluggedIn = true))
        assertNull(Logic.learnDischargeSign(-20_000, pluggedIn = false))
    }

    @Test fun phoneIsSentOnChangeAndAsAHeartbeatWhileCharging() {
        val a = Logic.PhoneReport(80, true, 4.1)
        assertTrue(Logic.shouldSendPhone(null, 0, a, 0))
        assertFalse(Logic.shouldSendPhone(a, 0, a.copy(watts = 4.3), 5_000))
        assertTrue(Logic.shouldSendPhone(a, 0, a.copy(watts = 4.3), Logic.PHONE_HEARTBEAT_MS))
        assertTrue(Logic.shouldSendPhone(a, 0, a.copy(percent = 81), 1_000))
        val off = a.copy(charging = false)
        assertTrue(Logic.shouldSendPhone(a, 0, off, 1_000))
        assertFalse(Logic.shouldSendPhone(off, 0, off, 600_000))  // nothing to keep alive
    }

    @Test fun standTextFoldsToTheStandsFont() {
        assertEquals("Beyonce - Deja vu", Logic.standText("Beyoncé — Déjà vu", 60))
        assertEquals("Don't Stop Me Now", Logic.standText("Don’t Stop Me Now", 60))
        assertEquals("Ilayaraja Hits", Logic.standText("Ilayaraja Hits 🎵", 60))
        assertEquals("", Logic.standText("മലയാളം", 60))  // nothing the stand can draw
        assertEquals("Strasse", Logic.standText("Straße", 60))
        assertEquals("A very l…", Logic.standText("A very long title", 9))
        assertEquals("a b", Logic.standText("  a \n  b ", 10))
    }

    @Test fun rgb565IsHighByteFirst() {
        val px = intArrayOf(0xFFFF0000.toInt(), 0xFF00FF00.toInt(), 0xFF0000FF.toInt(), 0xFFFFFFFF.toInt())
        assertArrayEquals(
            byteArrayOf(0xF8.toByte(), 0x00, 0x07, 0xE0.toByte(), 0x00, 0x1F, 0xFF.toByte(), 0xFF.toByte()),
            Logic.rgb565(px),
        )
    }

    @Test fun artIdIsStableAndShort() {
        val a = Logic.artId("app", "Song", "Artist", 512, 512)
        assertEquals(a, Logic.artId("app", "Song", "Artist", 512, 512))
        assertEquals(12, a.length)
        assertTrue(a != Logic.artId("app", "Song 2", "Artist", 512, 512))
    }

    @Test fun positionMovesOnlyWhilePlaying() {
        assertEquals(15_000, Logic.positionNow(10_000, 1_000, 1f, 6_000, playing = true))
        assertEquals(10_000, Logic.positionNow(10_000, 1_000, 1f, 6_000, playing = false))
        assertEquals(-1, Logic.positionNow(-1, 1_000, 1f, 6_000, playing = true))
    }

    @Test fun screenshotDecodes() {
        val w = Logic.SCREEN
        val bmp = ByteArray(66 + w * w * 2)
        fun put16(i: Int, v: Int) { bmp[i] = v.toByte(); bmp[i + 1] = (v shr 8).toByte() }
        fun put32(i: Int, v: Int) { put16(i, v and 0xFFFF); put16(i + 2, v ushr 16) }
        bmp[0] = 'B'.code.toByte(); bmp[1] = 'M'.code.toByte()
        put32(10, 66); put32(18, w); put32(22, -w); put16(28, 16)
        put16(66, 0xF800)                       // first pixel red
        put16(66 + (w * w - 1) * 2, 0x001F)     // last pixel blue
        val px = Logic.decodeScreenshot(bmp)!!
        assertEquals(0xFFFF0000.toInt(), px[0])
        assertEquals(0xFF0000FF.toInt(), px[w * w - 1])
        assertNull(Logic.decodeScreenshot(ByteArray(10)))
    }

    @Test fun rleScreenshotDecodes() {
        val w = Logic.SCREEN
        val b = java.io.ByteArrayOutputStream()
        b.write("W6R1".toByteArray()); b.write(byteArrayOf(w.toByte(), 0, w.toByte(), 0))
        b.write(1); b.write(byteArrayOf(0x00, 0xF8.toByte(), 0x1F, 0x00))   // two literal pixels: red, blue
        var left = w * w - 2
        while (left > 0) { val n = minOf(128, left); b.write(0x80 or (n - 1)); b.write(byteArrayOf(0, 0)); left -= n }
        val px = Logic.decodeScreenshot(b.toByteArray())!!
        assertEquals(0xFFFF0000.toInt(), px[0])
        assertEquals(0xFF0000FF.toInt(), px[1])
        assertEquals(0xFF000000.toInt(), px[w * w - 1])
        assertNull(Logic.decodeRle(b.toByteArray().copyOf(b.size() - 3)))   // cut short
    }

    @Test fun hostsAreNormalized() {
        assertEquals("w6.local", Logic.normalizeHost(" w6.local "))
        assertEquals("192.168.0.50", Logic.normalizeHost("http://192.168.0.50/"))
        assertEquals("192.168.0.50:8080", Logic.normalizeHost("192.168.0.50:8080"))
        assertNull(Logic.normalizeHost("https://w6.local"))
        assertNull(Logic.normalizeHost("not an address"))
        assertNull(Logic.normalizeHost(""))
    }

    @Test fun pairCodesAreNormalized() {
        assertEquals("k7m2p9xq", Logic.pairCode("K7M2 P9XQ"))
        assertEquals("k7m2p9xq", Logic.pairCode("k7m2-p9xq"))
        assertNull(Logic.pairCode("K7M2P9X"))       // too short
        assertNull(Logic.pairCode("K7M2P9XO"))      // O isn't in the stand's alphabet
    }

    @Test fun pairLinksAreReadAndKeptLocal() {
        assertEquals(Logic.PairLink("192.168.0.50", "k7m2p9xq"), Logic.pairLink("w6://pair?host=192.168.0.50&code=k7m2p9xq"))
        assertEquals(Logic.PairLink("w6.local", "k7m2p9xq"), Logic.pairLink("W6://pair?code=K7M2P9XQ&host=w6.local"))
        assertNull(Logic.pairLink("w6://pair?host=203.0.113.5&code=k7m2p9xq"))   // not a home network
        assertNull(Logic.pairLink("w6://pair?host=example.com&code=k7m2p9xq"))
        assertNull(Logic.pairLink("w6://pair?host=192.168.0.50"))
        assertNull(Logic.pairLink("https://example.com/?host=192.168.0.50&code=k7m2p9xq"))
        assertTrue(Logic.isLocal("10.0.0.7"))
        assertTrue(Logic.isLocal("172.20.1.1"))
        assertFalse(Logic.isLocal("172.32.1.1"))
    }

    @Test fun wording() {
        assertEquals("24:59", Logic.clockDuration(1499))
        assertEquals("1:02:03", Logic.clockDuration(3723))
        assertEquals("2h 05m", Logic.duration(7500))
        assertEquals("under 10 s", Logic.lostPowerRan(0))
        assertEquals("1–10 min", Logic.lostPowerRan(60))
    }

    @Test fun statusParsesOldAndNewFirmware() {
        val new = StandStatus.parse(JSONObject("""{"version":"0.3.0","uptime_s":71,"reset_reason":"restart","rssi_dbm":-41,
            "free_heap":76560,"rotation":1,"face":"classic","chip_temp_c":52.7,"place":"Mumbai","features":["phone","media"],
            "timer_left_s":120,"weather":{"temp_c":28.7,"high_c":29.8,"low_c":24.9,"wmo_code":3,"age_s":605}}"""))
        assertTrue(new.has("media"))
        assertEquals(120L, new.timerLeftS)
        assertNull(new.clock24h)  // 0.3.0 had no setting
        assertEquals(false, StandStatus.parse(JSONObject("""{"version":"0.3.7","clock_24h":false}""")).clock24h)
        val old = StandStatus.parse(JSONObject("""{"version":"0.1.7","uptime_s":5,"place":null,"weather":null}"""))
        assertFalse(old.has("media"))
        assertNull(old.utcOffsetS)
        assertNull(old.timerLeftS)
        assertNull(old.faces)  // before 0.4.0: the app offers classic, sun and analog
        assertEquals(listOf("classic", "stack_calm"),
            StandStatus.parse(JSONObject("""{"version":"0.4.0","faces":["classic","stack_calm"]}""")).faces)
        assertEquals(19800, StandStatus.parse(JSONObject("""{"version":"0.5.0","utc_offset_s":19800}""")).utcOffsetS)
        val styles = StandStatus.parse(JSONObject("""{"version":"0.6.3","charging_style":"warp","charging_styles":["classic","warp","swarm"]}"""))
        assertEquals("warp", styles.chargingStyle)
        assertEquals(listOf("classic", "warp", "swarm"), styles.chargingStyles)
        assertNull(old.chargingStyles)  // before 0.6.3: the charging screen is always the classic ring
    }

    @Test fun utcOffsetFollowsTheZoneAndDaylightSaving() {
        val at = java.time.Instant.parse("2026-01-15T12:00:00Z")
        assertEquals(19800, Logic.utcOffsetSeconds(java.time.ZoneId.of("Asia/Kolkata"), at))
        assertEquals(20700, Logic.utcOffsetSeconds(java.time.ZoneId.of("Asia/Kathmandu"), at))
        assertEquals(0, Logic.utcOffsetSeconds(java.time.ZoneId.of("Europe/London"), at))
        assertEquals(3600, Logic.utcOffsetSeconds(java.time.ZoneId.of("Europe/London"), java.time.Instant.parse("2026-07-15T12:00:00Z")))
    }

    @Test fun logParses() {
        val log = StandLog.parse(JSONObject("""{"boots":[{"n":8,"started":1791700000,"reason":"restart","ran_s":47,
            "ran_exact":true,"running":true,"got_online":true,"setup_by_plugging":false,"wifi_drops":0,"max_chip_c":71,
            "min_free_heap":90000},{"n":7,"started":null,"reason":"power on","ran_s":602,"ran_exact":true,"running":false,
            "got_online":false,"setup_by_plugging":true,"wifi_drops":0,"min_free_heap":80000,"restarted_for":"setup idle"}],
            "wifi_drops":[{"uptime_s":3,"code":39,"reason":"timed out","while_joining":true}],"wifi_drops_total":1}"""))
        assertEquals(2, log.boots.size)
        assertNull(log.boots[1].startedUnix)
        assertEquals("setup idle", log.boots[1].restartedFor)
        assertTrue(log.wifi[0].whileJoining)
    }
}
