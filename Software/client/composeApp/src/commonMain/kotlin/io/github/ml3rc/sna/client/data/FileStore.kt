package io.github.ml3rc.sna.client.data

/**
 * Platform file save/open. Desktop uses a native file dialog (real, unrestricted paths).
 *
 * Android implementation is a SCAFFOLD SIMPLIFICATION: it reads/writes the app-private external
 * files directory rather than prompting a proper Storage Access Framework document picker, so
 * there's no "pick a real save location" UX yet and saved files aren't visible in e.g. Downloads
 * or easily shareable. Replace with an ACTION_CREATE_DOCUMENT / ACTION_OPEN_DOCUMENT activity
 * result flow before shipping -- see AndroidFileContext.kt.
 */
expect object FileStore {
    /** Returns true if the user completed the save (false if they cancelled). */
    suspend fun save(suggestedName: String, content: String): Boolean

    /** Returns (filename, content), or null if the user cancelled / nothing was found. */
    suspend fun open(): Pair<String, String>?
}
