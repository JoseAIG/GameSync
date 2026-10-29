package com.savesync.android.ui.screens

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CatalogErrorCopyTest {

    private val retry = "Retrying when the server comes back"

    @Test
    fun `a rejected key says so and promises no retry`() {
        val copy = catalogErrorCopy("GET /roms/fingerprints: HTTP 401", "nas")
        assertEquals("nas rejected the API key", copy.title)
        assertFalse(retry in copy.footnotes)
        assertEquals("nas rejected the API key", catalogErrorCopy("HTTP 403 Forbidden", "nas").title)
    }

    @Test
    fun `a server error or an odd reply is not a network problem`() {
        assertEquals("nas answered with an error", catalogErrorCopy("HTTP 500 Internal Server Error", "nas").title)
        val parse = catalogErrorCopy("Expected BEGIN_OBJECT but was STRING at line 1 column 1", "nas")
        assertEquals("Unexpected reply from nas", parse.title)
        assertFalse(retry in parse.footnotes)
    }

    @Test
    fun `a network failure keeps the couldn't-reach copy and its retry`() {
        val copy = catalogErrorCopy("Unable to resolve host \"nas\": No address associated with hostname", "nas")
        assertEquals("Couldn't reach nas", copy.title)
        assertTrue(retry in copy.footnotes)
    }
}
