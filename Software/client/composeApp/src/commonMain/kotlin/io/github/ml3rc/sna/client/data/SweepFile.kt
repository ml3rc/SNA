package io.github.ml3rc.sna.client.data

import io.github.ml3rc.sna.client.protocol.SweepPoint
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json

/** Native round-trip format -- mirrors the `sweep` response shape from PROTOCOL.md so a saved
 *  file can be reloaded into the app without lossy conversion (unlike Touchstone, which drops
 *  the fact that this is a scalar-only instrument once written). */
@Serializable
data class SweepFile(
    val formatVersion: Int = 1,
    val z: Int,
    val f0: Long,
    val f1: Long,
    val points: List<SweepPoint>,
)

object SweepFileIO {
    private val json = Json { prettyPrint = true; encodeDefaults = true }

    fun encode(file: SweepFile): String = json.encodeToString(SweepFile.serializer(), file)

    fun decode(text: String): SweepFile = json.decodeFromString(SweepFile.serializer(), text)
}
