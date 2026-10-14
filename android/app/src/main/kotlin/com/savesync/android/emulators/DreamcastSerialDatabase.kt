package com.savesync.android.emulators

import com.savesync.android.SaveSyncApp

/**
 * Parses the bundled "Sega - Dreamcast (libretro).dat" clrmamepro DAT file (from app assets)
 * to build a case-insensitive game-name → disc-serial lookup table.
 *
 * Dreamcast saves are keyed by the disc's serial (the IP.BIN product number), not by a name
 * slug — see `shared/rom_id/dreamcast.py`.  That is what MemCard PRO DC and openMenu's Serial
 * VMU name their per-game folders after, so a Flycast save on this device and a card save on
 * real hardware only share a server slot if both sides produce the same `DC_<serial>` id.
 *
 * This DAT lookup is the fallback for when the IP.BIN product code can't be read straight out
 * of the disc image — most importantly CHDs, which are compressed.
 *
 * Lookup strategy ([lookupSerial]) mirrors [SaturnSerialDatabase]:
 *  1. Strip `[bracket]` tags (translation / hack suffixes added by the patcher).
 *  2. Try exact match against the DAT game name (case-insensitive).
 *  3. Progressively strip trailing `(...)` parenthetical groups and retry, so that
 *     "Shenmue (USA) (Disc 1) [T-En]" still matches "Shenmue (USA)" in the DAT.
 *
 * Thread-safe: [nameToSerial] is written once under [load] and thereafter only read.
 */
internal object DreamcastSerialDatabase {

    private const val ASSET_NAME = "Sega - Dreamcast (libretro).dat"

    /** Lowercase game name → disc serial as the DAT spells it (e.g. "T-1249M", "51000"). */
    @Volatile
    private var nameToSerial: Map<String, String>? = null

    /** Canonical serial → game name, over *every* serial a game lists. */
    @Volatile
    private var serialToName: Map<String, String>? = null

    /** Strips `[text]` bracket tags (fan-translation / hack markers). */
    private val bracketTagRe = Regex("""\s*\[[^\]]*\]""")

    /** Strips the last `(...)` group from the end of a string. */
    private val trailingParenRe = Regex("""\s*\([^)]*\)\s*$""")

    /**
     * Returns the disc serial for [romName] (bare filename without extension),
     * or null if the DAT has no match.  The result is the DAT's own spelling —
     * call [DreamcastSerial.canonical] before building a title ID.
     */
    fun lookupSerial(romName: String): String? {
        val db = nameToSerial ?: load()

        // Step 1: strip [bracket] tags
        var name = romName.replace(bracketTagRe, "").trim()

        // Step 2: exact match (case-insensitive)
        db[name.lowercase()]?.let { return it }

        // Step 3: progressively strip trailing (...) groups and retry
        while (true) {
            val stripped = trailingParenRe.replace(name, "").trim()
            if (stripped == name || stripped.isBlank()) break
            name = stripped
            db[name.lowercase()]?.let { return it }
        }

        return null
    }

    /**
     * The DAT's game name for a disc serial (any spelling: `T-1401D-50`,
     * `T1401D__50`), or null.  Uses every serial a game carries — the DAT
     * often lists a Sega Europe box number (`950-0019-50`) before the IP.BIN
     * product number (`T-1401D-50`) a save device files by.
     */
    fun nameForSerial(serial: String): String? {
        if (serialToName == null) load()
        return serialToName?.get(DreamcastSerial.canonical(serial))
    }

    // ----------------------------------------------------------------------------------
    // Initialisation
    // ----------------------------------------------------------------------------------

    private fun load(): Map<String, String> {
        // Double-checked: another thread may have initialised while we waited.
        nameToSerial?.let { return it }
        val entries = try {
            SaveSyncApp.instance.assets.open(ASSET_NAME).use { parseDatEntries(it) }
        } catch (_: Exception) {
            emptyList()
        }
        val reverse = mutableMapOf<String, String>()
        for ((name, serial) in entries) reverse.putIfAbsent(DreamcastSerial.canonical(serial), name)
        serialToName = reverse
        val parsed = namesToFirstSerial(entries)
        nameToSerial = parsed
        return parsed
    }

    // ----------------------------------------------------------------------------------
    // DAT parser (clrmamepro format)
    // ----------------------------------------------------------------------------------

    /**
     * Parses the clrmamepro DAT stream and returns lowercase game name → serial.
     *
     * ```
     * game (
     *     name "Capcom vs. SNK 2 - Millionaire Fighting 2001 (Japan)"
     *     region "Japan"
     *     serial "T-1249M"
     *     rom ( name "..." ... serial "T-1249M" )
     * )
     * ```
     * The game-level `serial` field (at the first indent level) is used; the per-rom copy
     * inside `rom ( ... )` is ignored.  When several entries share a name, the first
     * (usually the primary region) is kept - unless it is a Sega box code
     * ([DreamcastSerial.isBoxCode]) and a later entry has the disc's product number.
     */
    internal fun parseDat(stream: java.io.InputStream): Map<String, String> =
        namesToFirstSerial(parseDatEntries(stream))

    private fun namesToFirstSerial(entries: List<Pair<String, String>>): Map<String, String> {
        val result = mutableMapOf<String, String>()
        for ((name, serial) in entries) {
            val key = name.lowercase()
            val have = result[key]
            if (have == null || (DreamcastSerial.isBoxCode(have) && !DreamcastSerial.isBoxCode(serial)))
                result[key] = serial
        }
        return result
    }

    /** Every `(game name, game-level serial)` in DAT order, duplicates kept. */
    internal fun parseDatEntries(stream: java.io.InputStream): List<Pair<String, String>> {
        val result = mutableListOf<Pair<String, String>>()
        var inGame = false
        var curName = ""
        var curSerial = ""

        stream.bufferedReader(Charsets.UTF_8).useLines { lines ->
            for (line in lines) {
                val s = line.trim()
                when {
                    s == "game (" -> {
                        inGame = true
                        curName = ""
                        curSerial = ""
                    }
                    s == ")" && inGame -> {
                        if (curName.isNotEmpty() && curSerial.isNotEmpty()) {
                            result += curName to curSerial
                        }
                        inGame = false
                    }
                    inGame -> {
                        // Only capture the first occurrence of each field per game block.
                        if (curName.isEmpty()) {
                            val m = Regex("""\s+name\s+"(.+)"""").find(line)
                            if (m != null) { curName = m.groupValues[1]; continue }
                        }
                        if (curSerial.isEmpty()) {
                            // Game-level serial lines only (single indent, not inside rom (...)).
                            val m = Regex("""^\s+serial\s+"([^"]+)"""").find(line)
                            if (m != null) curSerial = m.groupValues[1]
                        }
                    }
                }
            }
        }
        return result
    }
}

/**
 * Canonical form of a Dreamcast disc serial, and the `DC_<serial>` title ID built from it.
 *
 * Kept in lockstep with `shared/rom_id/dreamcast.py` — every client has to fold serials the
 * same way or the same game lands in two server slots.
 *
 * Sega's own releases are spelled inconsistently between the disc and the Redump DAT:
 * ```
 * Sonic Adventure (USA)   IP.BIN "MK-51000"      DAT serial "51000"
 * 18 Wheeler (Europe)     IP.BIN "MK-51064-50"   DAT serial "MK-51064-50"
 * ```
 * so the canonical form drops punctuation and Sega's `MK` publisher prefix.  Third-party
 * codes (`T-1249M`) and Sega Japan's (`HDR-0080`) are unaffected.  The region suffix is
 * deliberately kept: `MK-51064-50` is the PAL disc and `MK-51064` the NTSC one.
 */
internal object DreamcastSerial {

    private val nonAlnumRe = Regex("[^A-Za-z0-9]")
    /** Sega's publisher prefix — only ever followed by the numeric product code. */
    private val segaPrefixRe = Regex("^MK(?=\\d)")

    /** "MK-51000" / "51000" / "t-1249m" → "51000" / "51000" / "T1249M". */
    fun canonical(serial: String): String =
        segaPrefixRe.replace(nonAlnumRe.replace(serial, "").uppercase(), "")

    private val boxCodeRe = Regex("""^9\d{2}-?\d{4}-?\d{2}$""")

    /**
     * A Sega Europe box code (`950-0019-50`): never what IP.BIN says.  Redump lists one
     * beside the disc's product number for many PAL Sega titles (Soulcalibur: `950-0019-50`
     * and `T-1401D-50`); the disc, and so every save device, only knows the second.
     */
    fun isBoxCode(serial: String): Boolean = boxCodeRe.matches(serial.trim())

    /** Canonical `DC_<serial>` title ID, or null when [serial] carries nothing usable. */
    fun titleId(serial: String?): String? {
        val canonical = canonical(serial ?: return null)
        return if (canonical.isBlank()) null else "DC_$canonical"
    }
}

/**
 * Reads the Dreamcast product number straight out of a disc image.
 *
 * The IP.BIN header starts every data track: hardware ID "SEGA SEGAKATANA " at byte 0,
 * product number at offset 0x40 (10 bytes).  Layouts handled:
 *
 *  - `.iso`                    — 2048 B/sector, header at file offset 0
 *  - `.bin` / `.img` / `.cdi`  — 2352 B/sector, header at offset 0x10 (after sync + header)
 *  - `.gdi`                    — sheet: each referenced track is tried in turn
 *
 * Rather than branch on the extension, the first 64 KB of a track is searched for the magic,
 * which covers both sector layouts and a CDI's leading descriptors.  `.chd` is compressed and
 * cannot be read this way — callers fall back to [DreamcastSerialDatabase].
 *
 * Mirrors `read_ip_bin` in `shared/rom_id/dreamcast.py`.
 */
internal object DreamcastDisc {

    private val MAGIC = "SEGA SEGAKATANA ".toByteArray(Charsets.US_ASCII)

    /** Header + product field must both fit in the window we search. */
    private const val WINDOW_BYTES = 64 * 1024
    private const val PRODUCT_OFFSET = 0x40
    private const val PRODUCT_LENGTH = 10

    /** A `.gdi` line: `3 45000 4 2352 track03.bin 0` — the 5th field is the filename. */
    private val gdiLineRe =
        Regex("""^\s*\d+\s+\d+\s+\d+\s+\d+\s+("[^"]+"|\S+)\s+\d+\s*$""")

    /**
     * Returns the raw product number (e.g. "MK-51000", "T-1249M"), or null when [romFile]
     * isn't a Dreamcast image this can read.
     */
    fun readProductCode(romFile: java.io.File): String? {
        return try {
            val tracks = if (romFile.extension.lowercase() == "gdi") {
                romFile.readLines().drop(1).mapNotNull { line ->
                    val name = gdiLineRe.find(line)?.groupValues?.getOrNull(1)?.trim('"')
                        ?: return@mapNotNull null
                    java.io.File(romFile.parent, name).takeIf { it.exists() }
                }
            } else {
                listOf(romFile)
            }
            tracks.firstNotNullOfOrNull { readFromTrack(it) }
        } catch (_: Exception) {
            null
        }
    }

    private fun readFromTrack(image: java.io.File): String? {
        val window = ByteArray(WINDOW_BYTES)
        val read = try {
            image.inputStream().use { it.read(window) }
        } catch (_: Exception) {
            return null
        }
        val last = read - MAGIC.size - PRODUCT_OFFSET - PRODUCT_LENGTH
        if (last < 0) return null

        for (i in 0..last) {
            var matched = true
            for (j in MAGIC.indices) {
                if (window[i + j] != MAGIC[j]) { matched = false; break }
            }
            if (!matched) continue
            val raw = String(window, i + PRODUCT_OFFSET, PRODUCT_LENGTH, Charsets.US_ASCII).trim()
            return raw.ifBlank { null }
        }
        return null
    }
}
