package io.github.ml3rc.sna.client.data

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.ComponentActivity
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.IOException

/**
 * The Storage Access Framework pickers, registered by the current activity in onCreate
 * ([register]) so a picker result also reaches an activity recreated meanwhile.
 */
object AndroidFileContext {
    lateinit var appContext: Context

    private var createDocument: ActivityResultLauncher<String>? = null
    private var openDocument: ActivityResultLauncher<Array<String>>? = null
    private var pending: CompletableDeferred<Uri?>? = null
    internal val pickers = Mutex() // one picker at a time

    fun register(activity: ComponentActivity) {
        appContext = activity.applicationContext
        createDocument = activity.registerForActivityResult(
            ActivityResultContracts.CreateDocument("application/octet-stream")) { pending?.complete(it) }
        openDocument = activity.registerForActivityResult(
            ActivityResultContracts.OpenDocument()) { pending?.complete(it) }
    }

    internal suspend fun pickToCreate(name: String): Uri? =
        pick { (createDocument ?: throw IOException("No activity to show the file picker")).launch(name) }

    internal suspend fun pickToOpen(): Uri? =
        pick { (openDocument ?: throw IOException("No activity to show the file picker")).launch(arrayOf("*/*")) }

    private suspend fun pick(launch: () -> Unit): Uri? = withContext(Dispatchers.Main) {
        val result = CompletableDeferred<Uri?>()
        pending = result
        try {
            launch()
            result.await()
        } finally {
            if (pending === result) pending = null
        }
    }
}

actual object FileStore {
    actual suspend fun save(suggestedName: String, content: String): Boolean = AndroidFileContext.pickers.withLock {
        val uri = AndroidFileContext.pickToCreate(suggestedName) ?: return@withLock false
        withContext(Dispatchers.IO) {
            val stream = AndroidFileContext.appContext.contentResolver.openOutputStream(uri, "wt")
                ?: throw IOException("Cannot write $uri")
            stream.use { it.write(content.toByteArray()) }
        }
        true
    }

    actual suspend fun open(): Pair<String, String>? = AndroidFileContext.pickers.withLock {
        val uri = AndroidFileContext.pickToOpen() ?: return@withLock null
        withContext(Dispatchers.IO) {
            val resolver = AndroidFileContext.appContext.contentResolver
            val name = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
                if (it.moveToFirst()) it.getString(0) else null
            } ?: uri.lastPathSegment ?: "file"
            val text = resolver.openInputStream(uri)?.use { it.readBytes().decodeToString() }
                ?: throw IOException("Cannot read $uri")
            name to text
        }
    }
}
