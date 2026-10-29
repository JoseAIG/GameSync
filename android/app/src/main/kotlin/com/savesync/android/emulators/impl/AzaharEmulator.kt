// Save layout based on analysis of Azahar (https://github.com/azahar-emu/azahar,
// GPL-2.0-or-later): src/core/hle/service/fs/archive.h and
// src/core/file_sys/archive_source_sd_savedata.cpp.  No code copied.
package com.savesync.android.emulators.impl

import com.savesync.android.emulators.DirCheck
import com.savesync.android.emulators.EmulatorBase
import com.savesync.android.emulators.SaveEntry
import java.io.File

class AzaharEmulator(
    private val candidateTitleRoots: List<File>? = null,
    private val storageBaseDir: File? = null,
    /**
     * Optional explicit save folder override, configured in the Emulator
     * Configuration screen.  May point at Azahar's user folder (the one that
     * holds ``sdmc/``), at ``sdmc``, ``Nintendo 3DS`` or directly at the
     * ``sdmc/Nintendo 3DS/<id0>/<id1>/title`` root — see
     * [resolveOverrideTitleRoot].  When set it replaces auto-detection
     * entirely: a missing override folder yields no saves rather than
     * silently syncing whatever auto-detection would find instead.
     */
    private val saveDirOverride: String? = null
) : EmulatorBase() {

    override val name: String = "Azahar"
    override val systemPrefix: String = "3DS"

    override fun discoverSaves(): List<SaveEntry> {
        val titleRoot = resolveTitleRoot(allowNonExistent = false) ?: return emptyList()
        if (!titleRoot.exists() || !titleRoot.isDirectory) return emptyList()

        val results = mutableListOf<SaveEntry>()

        titleRoot.listFiles()
            ?.filter { it.isDirectory && HEX8_REGEX.matches(it.name) }
            ?.sortedBy { it.name }
            ?.forEach { highDir ->
                highDir.listFiles()
                    ?.filter { it.isDirectory && HEX8_REGEX.matches(it.name) }
                    ?.sortedBy { it.name }
                    ?.forEach { lowDir ->
                        val saveDir = File(lowDir, "data/00000001")
                        if (!saveDir.exists() || !saveDir.isDirectory) return@forEach
                        val files = saveDir.walkTopDown().filter { it.isFile }.toList()
                        if (files.isEmpty()) return@forEach

                        val titleId = (highDir.name + lowDir.name).uppercase()
                        results.add(
                            SaveEntry(
                                titleId = titleId,
                                displayName = titleId,
                                systemName = "3DS",
                                saveFile = null,
                                saveDir = saveDir,
                                isMultiFile = true
                            )
                        )
                    }
            }

        return results
    }

    private fun resolveTitleRoot(allowNonExistent: Boolean): File? {
        if (!saveDirOverride.isNullOrBlank()) {
            // No fallback to auto-detection: syncing a different folder than
            // the one the user picked is worse than syncing nothing.  The UI
            // surfaces the problem via [overrideWarning].
            val titleRoot = resolveOverrideTitleRoot(File(saveDirOverride))
            return titleRoot.takeIf { allowNonExistent || it.isDirectory }
        }
        // Only touch Environment (via baseDir) when there is nothing explicit.
        if (candidateTitleRoots != null) {
            return if (allowNonExistent) candidateTitleRoots.firstOrNull()
                   else candidateTitleRoots.firstOrNull { it.isDirectory }
        }
        return autoDetectedTitleRoot(buildCandidateTitleRoots(storageBaseDir ?: baseDir), allowNonExistent)
    }

    companion object {
        /** Key used in [com.savesync.android.storage.Settings.saveDirOverrides]. */
        const val EMULATOR_KEY = "Azahar"

        /**
         * Azahar (like Citra before it) hardcodes both ID0 and ID1 to 32
         * zeros — ``SYSTEM_ID`` / ``SDCARD_ID`` in
         * src/core/hle/service/fs/archive.h of github.com/azahar-emu/azahar
         * (GPL-2.0-or-later).  Other 32-hex IDs are still accepted (copied
         * real-SD layouts, forks) but the zero pair is preferred.
         */
        private const val ZERO_ID = "00000000000000000000000000000000"
        private val HEX8_REGEX = Regex("^[0-9A-Fa-f]{8}$")
        private val HEX32_REGEX = Regex("^[0-9A-Fa-f]{32}$")
        private val TITLE_ID_REGEX = Regex("^[0-9A-Fa-f]{16}$")

        /**
         * Maps a user-picked override folder to the ``title`` root.  Users
         * naturally pick Azahar's user folder (chosen in Azahar's first-run
         * setup, e.g. ``/storage/emulated/0/Azahar``) rather than browsing
         * five levels down, so every level of
         * ``<user>/sdmc/Nintendo 3DS/<id0>/<id1>/title`` is accepted.  Any
         * other folder is the title root itself only when it already holds
         * ``<high>/<low>`` title folders; otherwise it is read as a user
         * folder whose ``sdmc`` Azahar hasn't created yet, so the root is
         * predicted under it (missing until then, which [overrideWarning]
         * reports and which stops downloads from creating a stray tree that
         * the real one would later replace).
         */
        fun resolveOverrideTitleRoot(overrideDir: File): File {
            if (overrideDir.name.equals("title", ignoreCase = true)) return overrideDir
            val sdmc = File(overrideDir, "sdmc")
            return when {
                sdmc.isDirectory -> titleRootUnder(File(sdmc, "Nintendo 3DS"))
                overrideDir.name.equals("sdmc", ignoreCase = true) ->
                    titleRootUnder(File(overrideDir, "Nintendo 3DS"))
                overrideDir.name.equals("Nintendo 3DS", ignoreCase = true) ->
                    titleRootUnder(overrideDir)
                // <id1> folder
                File(overrideDir, "title").isDirectory -> File(overrideDir, "title")
                // <id0> folder
                HEX32_REGEX.matches(overrideDir.name) ->
                    hex32Children(overrideDir)
                        .map { File(it, "title") }
                        .firstOrNull { it.isDirectory }
                        ?: File(overrideDir, "$ZERO_ID/title")
                looksLikeTitleRoot(overrideDir) -> overrideDir
                else -> File(overrideDir, "sdmc/Nintendo 3DS/$ZERO_ID/$ZERO_ID/title")
            }
        }

        /** Whether [dir] holds ``<high>/<low>`` hex title folders, as a ``title`` root does. */
        private fun looksLikeTitleRoot(dir: File): Boolean =
            (dir.listFiles() ?: emptyArray()).any { high ->
                high.isDirectory && HEX8_REGEX.matches(high.name) &&
                    (high.listFiles() ?: emptyArray()).any { it.isDirectory && HEX8_REGEX.matches(it.name) }
            }

        /**
         * Human-readable problem with a configured override, or null when it
         * looks usable.  Shown in the Emulator Configuration screen and in the
         * sync result so a wrong/missing folder is never silent.
         */
        fun overrideWarning(overridePath: String?): String? {
            if (overridePath.isNullOrBlank()) return null
            val overrideDir = File(overridePath)
            if (!overrideDir.isDirectory) {
                return "Azahar save folder not found: $overridePath. " +
                    "3DS saves are not synced until it exists or the override is cleared."
            }
            val titleRoot = resolveOverrideTitleRoot(overrideDir)
            if (!titleRoot.isDirectory) {
                return "No 3DS saves yet under $overridePath " +
                    "(expected ${titleRoot.path}). Start a game in Azahar once, " +
                    "or pick Azahar's user folder (the one containing sdmc)."
            }
            return null
        }

        /**
         * What the folder picker shows for a candidate override: the
         * [overrideWarning] when there is one, else where the saves were
         * found and how many titles, e.g. "Found sdmc/Nintendo 3DS/…/title
         * (14 titles)".  ok only when at least one title save is there.
         * Reads the disk: call it off the main thread.
         */
        fun describeOverride(dir: File): DirCheck {
            overrideWarning(dir.path)?.let { return DirCheck(false, it) }
            val titleRoot = resolveOverrideTitleRoot(dir)
            val titles = countTitleSaves(titleRoot)
            val shown = displayTitleRoot(dir, titleRoot)
            return if (titles > 0) {
                DirCheck(true, "Found $shown (${if (titles == 1) "1 title" else "$titles titles"})")
            } else {
                DirCheck(
                    false,
                    "No 3DS saves in $shown yet. Start a game in Azahar once, " +
                        "or pick Azahar's user folder (the one containing sdmc).",
                )
            }
        }

        /** Title folders under [titleRoot] that hold a save (``<high>/<low>/data/00000001``). */
        private fun countTitleSaves(titleRoot: File): Int =
            (titleRoot.listFiles() ?: emptyArray())
                .filter { it.isDirectory && HEX8_REGEX.matches(it.name) }
                .sumOf { high ->
                    // Same rule as discoverSaves: an empty save folder syncs nothing.
                    (high.listFiles() ?: emptyArray()).count { low ->
                        low.isDirectory && HEX8_REGEX.matches(low.name) &&
                            File(low, "data/00000001").walkTopDown().any { it.isFile }
                    }
                }

        /** [titleRoot] relative to the picked [dir], the 32-hex ID folders shown as "…". */
        private fun displayTitleRoot(dir: File, titleRoot: File): String {
            val relative = titleRoot.absoluteFile.toRelativeString(dir.absoluteFile)
            if (relative.isEmpty()) return dir.name
            val parts = mutableListOf<String>()
            relative.split(File.separatorChar).forEach { seg ->
                if (HEX32_REGEX.matches(seg)) {
                    if (parts.lastOrNull() != "…") parts += "…"
                } else {
                    parts += seg
                }
            }
            return parts.joinToString("/")
        }

        /**
         * ``<Nintendo 3DS>/<id0>/<id1>/title`` — the zero-ID pair Azahar uses
         * if present, else any existing 32-hex pair, else the zero-ID path.
         */
        private fun titleRootUnder(nintendo3ds: File): File {
            val zero = File(nintendo3ds, "$ZERO_ID/$ZERO_ID/title")
            if (zero.isDirectory) return zero
            return hex32Children(nintendo3ds)
                .flatMap { id0 -> hex32Children(id0) }
                .map { id1 -> File(id1, "title") }
                .firstOrNull { it.isDirectory }
                ?: zero
        }

        private fun hex32Children(dir: File): List<File> =
            dir.listFiles()
                ?.filter { it.isDirectory && HEX32_REGEX.matches(it.name) }
                ?.sortedBy { it.name }
                ?: emptyList()

        fun findTitleRoot(
            storageBaseDir: File,
            allowNonExistent: Boolean = false,
            candidateTitleRoots: List<File>? = null
        ): File? {
            if (candidateTitleRoots != null) {
                return if (allowNonExistent) candidateTitleRoots.firstOrNull()
                       else candidateTitleRoots.firstOrNull { it.isDirectory }
            }
            return autoDetectedTitleRoot(buildCandidateTitleRoots(storageBaseDir), allowNonExistent)
        }

        /**
         * The auto-detected title root: the first candidate that exists —
         * the same one discovery reads.  A download target ([allowNonExistent])
         * may be predicted only under an ``sdmc`` folder Azahar already made;
         * guessing the first candidate instead would create a stray tree that
         * discovery then prefers, hiding the real saves.  Null = no Azahar here.
         */
        private fun autoDetectedTitleRoot(candidates: List<File>, allowNonExistent: Boolean): File? =
            candidates.firstOrNull { it.isDirectory }
                ?: if (allowNonExistent) candidates.firstOrNull { sdmcOf(it)?.isDirectory == true } else null

        /** ``<sdmc>`` of a ``<sdmc>/Nintendo 3DS/<id0>/<id1>/title`` root. */
        private fun sdmcOf(titleRoot: File): File? =
            titleRoot.parentFile?.parentFile?.parentFile?.parentFile

        fun defaultSaveDir(
            storageBaseDir: File,
            titleId: String,
            candidateTitleRoots: List<File>? = null
        ): File? {
            if (!TITLE_ID_REGEX.matches(titleId)) return null
            val titleRoot = findTitleRoot(
                storageBaseDir = storageBaseDir,
                allowNonExistent = true,
                candidateTitleRoots = candidateTitleRoots
            ) ?: return null
            // Azahar names these folders with lowercase hex ("{:08x}" in
            // archive_source_sd_savedata.cpp); match it so the tree looks the
            // same as one Azahar created, even on case-sensitive storage.
            val lower = titleId.lowercase()
            return File(
                File(
                    File(titleRoot, lower.substring(0, 8)),
                    lower.substring(8)
                ),
                "data/00000001"
            )
        }

        private fun buildCandidateTitleRoots(storageBaseDir: File): List<File> {
            val sdmcDirs = listOf(
                "sdmc",
                "Azahar/sdmc",
                "azahar/sdmc",
                "Azahar-emu/sdmc",
                "azahar-emu/sdmc",
                "citra-emu/sdmc",
                "lime3ds-emu/sdmc",
                "Android/data/org.azahar_emu.azahar/files/sdmc",
                "Android/data/org.citra.citra_emu/files/sdmc",
                "Android/data/io.github.lime3ds.android/files/sdmc",
            )
            return sdmcDirs.map { relative ->
                titleRootUnder(File(File(storageBaseDir, relative), "Nintendo 3DS"))
            }
        }
    }
}
