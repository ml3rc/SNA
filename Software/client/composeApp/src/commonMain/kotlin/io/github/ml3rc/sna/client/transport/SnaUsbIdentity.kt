package io.github.ml3rc.sna.client.transport

/** USB identity advertised by firmware/src/usb_descriptors.c.
 * Keep Android's res/xml/device_filter.xml in sync (it uses decimal IDs).
 * These are the existing development IDs; update all three when allocated IDs are assigned.
 */
object SnaUsbIdentity {
    const val VENDOR_ID = 0xCAFE
    const val PRODUCT_ID = 0x4001

    fun matches(vendorId: Int, productId: Int): Boolean =
        vendorId == VENDOR_ID && productId == PRODUCT_ID
}
