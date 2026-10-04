package io.github.ml3rc.sna.client.data

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import javax.swing.JFileChooser

actual object FileStore {
    actual suspend fun save(suggestedName: String, content: String): Boolean = withContext(Dispatchers.IO) {
        val chooser = JFileChooser().apply { selectedFile = File(suggestedName) }
        if (chooser.showSaveDialog(null) == JFileChooser.APPROVE_OPTION) {
            chooser.selectedFile.writeText(content)
            true
        } else {
            false
        }
    }

    actual suspend fun open(): Pair<String, String>? = withContext(Dispatchers.IO) {
        val chooser = JFileChooser()
        if (chooser.showOpenDialog(null) == JFileChooser.APPROVE_OPTION) {
            val file = chooser.selectedFile
            file.name to file.readText()
        } else {
            null
        }
    }
}
