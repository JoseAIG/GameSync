package com.savesync.android.ui.components

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class FolderPickerMotionTest {

    private val root = "/storage/emulated/0"

    @Test
    fun `drilling in slides in, going up slides back`() {
        assertEquals(PickerMotion.DRILL_IN, pickerMotionFor(root, "$root/ROMs", reduce = false))
        assertEquals(PickerMotion.DRILL_IN, pickerMotionFor(root, "$root/ROMs/psx", reduce = false))
        assertEquals(PickerMotion.GO_UP, pickerMotionFor("$root/ROMs/psx", root, reduce = false))
    }

    @Test
    fun `another volume, the first listing and reduced motion fade`() {
        assertEquals(PickerMotion.FADE, pickerMotionFor(root, "/storage/1234-ABCD", reduce = false))
        assertEquals(PickerMotion.FADE, pickerMotionFor(null, root, reduce = false))
        assertEquals(PickerMotion.FADE, pickerMotionFor(root, "$root/ROMs", reduce = true))
        assertEquals(PickerMotion.FADE, pickerMotionFor("$root/ROMs/psx", root, reduce = true))
    }

    @Test
    fun `a sibling with a shared name prefix is not a child`() {
        // "/a/ROMs2" is not inside "/a/ROMs".
        assertFalse(isAncestor("$root/ROMs", "$root/ROMs2"))
        assertEquals(PickerMotion.FADE, pickerMotionFor("$root/ROMs", "$root/ROMs2", reduce = false))
        assertTrue(isAncestor("/", "/storage"))
    }

    @Test
    fun `coming up focuses the child we left`() {
        assertEquals("ROMs", childToward(root, "$root/ROMs"))
        assertEquals("ROMs", childToward(root, "$root/ROMs/psx/deep"))
        assertNull(childToward("$root/ROMs", root))
        assertNull(childToward(root, root))
    }
}
