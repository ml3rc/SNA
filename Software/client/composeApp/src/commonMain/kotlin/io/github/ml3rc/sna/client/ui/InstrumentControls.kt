package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.material3.Button as MaterialButton
import androidx.compose.material3.OutlinedTextField as MaterialTextField
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp

@Composable
fun Button(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
    content: @Composable RowScope.() -> Unit) {
    MaterialButton(onClick, modifier.heightIn(min = 48.dp), enabled,
        shape = RoundedCornerShape(3.dp), elevation = null,
        colors = ButtonDefaults.buttonColors(containerColor = MaterialTheme.colorScheme.primary,
            disabledContainerColor = MaterialTheme.colorScheme.surfaceVariant,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)),
        contentPadding = PaddingValues(horizontal = 16.dp, vertical = 10.dp), content = content)
}

@Composable
fun OutlinedButton(onClick: () -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
    content: @Composable RowScope.() -> Unit) {
    MaterialButton(onClick, modifier.heightIn(min = 48.dp), enabled,
        shape = RoundedCornerShape(3.dp), elevation = null,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
        colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent,
            contentColor = MaterialTheme.colorScheme.onSurface,
            disabledContainerColor = MaterialTheme.colorScheme.surface,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)),
        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp), content = content)
}

@Composable
fun OutlinedTextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier,
    label: @Composable (() -> Unit)? = null, singleLine: Boolean = false,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default, enabled: Boolean = true) {
    MaterialTextField(value, onValueChange, modifier, enabled = enabled, singleLine = singleLine,
        label = label, keyboardOptions = keyboardOptions, shape = RoundedCornerShape(3.dp),
        textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace),
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = MaterialTheme.colorScheme.background,
            unfocusedContainerColor = MaterialTheme.colorScheme.background,
            unfocusedBorderColor = MaterialTheme.colorScheme.outline,
            focusedBorderColor = MaterialTheme.colorScheme.primary))
}

@Composable
fun Switch(checked: Boolean, onCheckedChange: ((Boolean) -> Unit)?, modifier: Modifier = Modifier, enabled: Boolean = true) {
    val accent = if (enabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline
    val outline = MaterialTheme.colorScheme.outline
    Box(modifier.width(62.dp).height(48.dp).semantics { contentDescription = "50 ohm impedance" }.toggleable(checked, enabled = enabled && onCheckedChange != null,
        role = Role.Switch, onValueChange = { onCheckedChange?.invoke(it) }), contentAlignment = Alignment.Center) {
        Canvas(Modifier.width(42.dp).height(20.dp)) {
            drawRect(outline.copy(alpha = .25f))
            drawRect(outline, style = Stroke(1.dp.toPx()))
            val inset = 3.dp.toPx()
            drawRect(accent, topLeft = Offset(if (checked) size.width / 2 else inset, inset),
                size = androidx.compose.ui.geometry.Size(size.width / 2 - inset, size.height - inset * 2))
        }
    }
}

@Composable
fun ToggleChip(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit,
    enabled: Boolean = true, accent: Color = MaterialTheme.colorScheme.primary) {
    val shape = RoundedCornerShape(3.dp)
    Row(Modifier.heightIn(min = 48.dp).background(if (checked) MaterialTheme.colorScheme.surfaceVariant else Color.Transparent, shape)
        .border(1.dp, if (checked) MaterialTheme.colorScheme.outline else MaterialTheme.colorScheme.outline, shape)
        .toggleable(checked, enabled, Role.Checkbox, onCheckedChange).padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Box(Modifier.size(5.dp).background(if (checked) accent else MaterialTheme.colorScheme.outline))
        Text(label, style = MaterialTheme.typography.labelLarge,
            color = if (!enabled) MaterialTheme.colorScheme.onSurfaceVariant else if (checked) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant)
    }
}

@Composable
fun ChoiceChip(label: String, selected: Boolean, onClick: () -> Unit) {
    val shape = RoundedCornerShape(3.dp)
    Box(Modifier.heightIn(min = 48.dp)
        .background(if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface, shape)
        .border(1.dp, if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline, shape)
        .selectable(selected, role = Role.RadioButton, onClick = onClick).padding(horizontal = 16.dp, vertical = 12.dp)) {
        Text(label, style = MaterialTheme.typography.labelLarge,
            color = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
