package io.github.ml3rc.sna.client.data

import kotlinx.coroutines.CancellationException

/**
 * Platform file save/open through the system's own picker: a native file dialog on desktop,
 * the Storage Access Framework (ACTION_CREATE_DOCUMENT / ACTION_OPEN_DOCUMENT) on Android.
 */
expect object FileStore {
    /** Returns true if the file was written, false if the user cancelled. Throws on I/O failure. */
    suspend fun save(suggestedName: String, content: String): Boolean

    /** Returns (filename, content), or null if the user cancelled. Throws on I/O failure. */
    suspend fun open(): Pair<String, String>?
}

/** [FileStore.save] for the UI: null when saved or cancelled, otherwise a message to show. */
suspend fun saveFile(suggestedName: String, content: String): String? = try {
    FileStore.save(suggestedName, content)
    null
} catch (e: CancellationException) {
    throw e
} catch (e: Exception) {
    "Export failed: ${e.message ?: e::class.simpleName}"
}
