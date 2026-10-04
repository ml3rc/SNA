package io.github.ml3rc.sna.client.data

import android.content.Context
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/** Set once from MainActivity.onCreate before any [FileStore] call. */
object AndroidFileContext {
    lateinit var appContext: Context
}

actual object FileStore {
    // SCAFFOLD SIMPLIFICATION -- see FileStore.kt. Writes to app-private external storage so
    // there's something real to build/run against; swap for SAF ACTION_CREATE_DOCUMENT /
    // ACTION_OPEN_DOCUMENT (via registerForActivityResult in MainActivity) before shipping.
    actual suspend fun save(suggestedName: String, content: String): Boolean = withContext(Dispatchers.IO) {
        val dir = AndroidFileContext.appContext.getExternalFilesDir(null) ?: return@withContext false
        File(dir, suggestedName).writeText(content)
        true
    }

    actual suspend fun open(): Pair<String, String>? = withContext(Dispatchers.IO) {
        val dir = AndroidFileContext.appContext.getExternalFilesDir(null) ?: return@withContext null
        val file = dir.listFiles()?.firstOrNull { it.isFile } ?: return@withContext null
        file.name to file.readText()
    }
}
