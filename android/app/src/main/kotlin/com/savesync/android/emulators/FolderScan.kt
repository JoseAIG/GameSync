package com.savesync.android.emulators

import com.savesync.android.installed.InstalledRomsScanner
import java.io.File

/**
 * What a folder holds, as the folder picker's contents line shows it.
 * [ok] = something GameSync recognises is there; when false the picker
 * draws the line in WarnText and asks before using the folder.
 */
data class DirCheck(val ok: Boolean, val summary: String)

/**
 * One-level look at a folder for the folder picker: ROMs by extension,
 * emulator saves, system folders and an EmuDeck layout.  It reuses the
 * scanner's own tables ([InstalledRomsScanner.ROM_EXTENSIONS],
 * [InstalledRomsScanner.SAVE_EXTENSIONS], [InstalledRomsScanner.SYSTEM_ROM_DIRS]),
 * so "recognisable" means what the scans will actually pick up.
 */
object FolderScan {

    /** Reads [dir] (one level, hidden entries skipped).  Disk I/O: call off the main thread. */
    fun describe(dir: File): DirCheck {
        val children = dir.listFiles()
            ?: return DirCheck(false, "Can't read this folder")
        val visible = children.filter { !it.name.startsWith(".") }
        return summarize(
            fileNames = visible.filter { it.isFile }.map { it.name },
            dirNames = visible.filter { it.isDirectory }.map { it.name },
        )
    }

    /** Folder name (any case) → system code, from the scanner's alias table. */
    private val SYSTEM_BY_FOLDER: Map<String, String> =
        InstalledRomsScanner.SYSTEM_ROM_DIRS.entries
            .flatMap { (system, names) -> names.map { it.lowercase() to system } }
            .toMap()

    private val EMUDECK_MARKERS = setOf("storage", "saves", "bios")

    /** The pure part of [describe]: names in, summary out. */
    fun summarize(fileNames: List<String>, dirNames: List<String>): DirCheck {
        if (fileNames.isEmpty() && dirNames.isEmpty()) return DirCheck(false, "Empty folder")

        val roms = fileNames.filter { ext(it) in InstalledRomsScanner.ROM_EXTENSIONS }
        val saves = fileNames.filter { ext(it) in InstalledRomsScanner.SAVE_EXTENSIONS }
        val otherFiles = fileNames.size - roms.size - saves.size

        val lowerDirs = dirNames.map { it.lowercase() }
        val emudeck = "roms" in lowerDirs && lowerDirs.any { it in EMUDECK_MARKERS }
        val systemDirs = dirNames.filter { it.lowercase() in SYSTEM_BY_FOLDER }
        val emudeckDirs = if (emudeck) dirNames.filter { it.lowercase() == "roms" || it.lowercase() in EMUDECK_MARKERS } else emptyList()
        val known = (systemDirs + emudeckDirs).toSet()
        val otherDirs = dirNames.count { it !in known }

        val parts = mutableListOf<String>()
        if (emudeck) parts += "EmuDeck folders (${emudeckDirs.sortedBy { it.lowercase() }.joinToString(", ")})"
        if (roms.isNotEmpty()) parts += "${plural(roms.size, "ROM")} (${topExtensions(roms)})"
        if (saves.isNotEmpty()) parts += "${plural(saves.size, "save")} (${topExtensions(saves)})"
        if (systemDirs.isNotEmpty()) {
            val names = systemDirs.sortedBy { it.lowercase() }
            val shown = names.take(3).joinToString(", ") + if (names.size > 3) ", +${names.size - 3}" else ""
            parts += "${plural(systemDirs.size, "system folder")} ($shown)"
        }
        val recognised = parts.isNotEmpty()
        if (otherDirs > 0) parts += plural(otherDirs, if (recognised) "other folder" else "folder")
        if (otherFiles > 0) parts += plural(otherFiles, if (recognised) "other file" else "file")

        val text = "Contains: " + parts.joinToString(", ")
        return if (recognised) DirCheck(true, text)
        else DirCheck(false, "$text — nothing GameSync recognises")
    }

    private fun ext(name: String): String = name.substringAfterLast('.', "").lowercase()

    /** ".3ds, .cia" — the most common extensions first, at most three. */
    private fun topExtensions(names: List<String>): String {
        val counts = names.groupingBy { ext(it) }.eachCount()
        val ordered = counts.entries.sortedWith(compareByDescending<Map.Entry<String, Int>> { it.value }.thenBy { it.key })
        val shown = ordered.take(3).joinToString(", ") { ".${it.key}" }
        return if (ordered.size > 3) "$shown, …" else shown
    }

    private fun plural(n: Int, noun: String): String = if (n == 1) "1 $noun" else "$n ${noun}s"
}
