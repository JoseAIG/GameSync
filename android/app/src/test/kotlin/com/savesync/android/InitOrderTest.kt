package com.savesync.android

import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

/**
 * Kotlin runs property initializers and `init` blocks in source order. An
 * `init` that starts work on Dispatchers.Main.immediate (viewModelScope)
 * runs that work inside the constructor, so a property declared *below* the
 * `init` is still null when it is touched. That crashed MainViewModel on
 * start (scanSaves() set _savesScanning before it existed), and no unit test
 * builds the real ViewModel, so this guards the source order instead: in
 * every class, all properties that hold state come before any `init`.
 * Getter-only properties are fine (they have no field to initialize).
 */
class InitOrderTest {

    private val sourceRoot = File("src/main/kotlin")

    private val propertyDecl = Regex(
        """^(?:(?:private|internal|protected|public|override|open|final|lateinit)\s+|@\w+(?:\([^)]*\))?\s+)*(?:val|var)\s+\w+"""
    )

    @Test
    fun `no class declares a stored property after an init block`() {
        assertTrue("source root not found: ${sourceRoot.absolutePath}", sourceRoot.isDirectory)
        val problems = mutableListOf<String>()
        sourceRoot.walkTopDown().filter { it.isFile && it.extension == "kt" }.forEach { file ->
            problems += problemsIn(file.readLines(), file.relativeTo(sourceRoot).path)
        }
        assertTrue(
            "Stored properties declared after an init block (move the init below them):\n" +
                problems.joinToString("\n"),
            problems.isEmpty(),
        )
    }

    @Test
    fun `the check catches a property below an init and ignores getters`() {
        val bad = """
            class A {
                init {
                    start()
                }
                private val flow = MutableStateFlow(false)
                val count: Int get() = 3
                val other: Int
                    get() = 4
            }
        """.trimIndent().lines()
        val found = problemsIn(bad, "A.kt")
        assertTrue(found.toString(), found.size == 1 && found[0].contains("A.kt:5"))
    }

    private fun problemsIn(lines: List<String>, name: String): List<String> {
        val out = mutableListOf<String>()
        lines.forEachIndexed { i, line ->
            val m = Regex("""^(\s*)init\s*\{""").find(line) ?: return@forEachIndexed
            val indent = m.groupValues[1]
            if (indent.isEmpty()) return@forEachIndexed
            val classClose = indent.dropLast(minOf(4, indent.length)) + "}"
            for (j in i + 1 until lines.size) {
                val l = lines[j]
                if (l.startsWith(classClose) && !l.startsWith(indent)) break
                if (!l.startsWith(indent) || l.length <= indent.length || l[indent.length].isWhitespace()) continue
                val body = l.substring(indent.length)
                if (!propertyDecl.containsMatchIn(body)) continue
                val getterOnly = body.contains(" get()") ||
                    lines.getOrNull(j + 1)?.trimStart()?.startsWith("get()") == true &&
                    !body.contains("=")
                if (!getterOnly) out += "$name:${j + 1}: ${body.trim()}"
            }
        }
        return out
    }
}
