package io.github.ml3rc.sna.client.transport

import kotlin.test.Test
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class SnaUsbIdentityTest {
    @Test
    fun acceptsFirmwareIdentity() {
        assertTrue(SnaUsbIdentity.matches(0xCAFE, 0x4001))
    }

    @Test
    fun rejectsUnrelatedAndUnknownDevices() {
        assertFalse(SnaUsbIdentity.matches(0x2E8A, 0x000C)) // Debug probe
        assertFalse(SnaUsbIdentity.matches(0xCAFE, 0x4002)) // Different product
        assertFalse(SnaUsbIdentity.matches(0x1234, 0x4001)) // Different vendor
        assertFalse(SnaUsbIdentity.matches(-1, -1)) // Unavailable desktop USB metadata
        assertFalse(SnaUsbIdentity.matches(0, 0))
    }
}
