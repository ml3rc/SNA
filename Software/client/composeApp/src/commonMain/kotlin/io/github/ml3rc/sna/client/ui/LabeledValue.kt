package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextAlign

/** A "label ........ value" readout row -- used on HK and anywhere else a bench-instrument-style
 *  key/value display reads better than a plain sentence. */
@Composable
fun LabeledValue(label: String, value: String, modifier: Modifier = Modifier) {
    Row(modifier.fillMaxWidth()) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
        Text(value, modifier = Modifier.weight(1f), fontFamily = FontFamily.Monospace,
            textAlign = TextAlign.End, color = MaterialTheme.colorScheme.onSurface)
    }
}
