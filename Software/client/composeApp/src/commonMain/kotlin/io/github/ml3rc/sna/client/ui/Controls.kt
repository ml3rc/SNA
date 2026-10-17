package io.github.ml3rc.sna.client.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.material3.Button as MaterialButton
import androidx.compose.material3.OutlinedTextField as MaterialTextField
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import io.github.ml3rc.sna.client.protocol.Impedance
import io.github.ml3rc.sna.client.protocol.ProtocolLimits

// Shared instrument widgets. Every input takes `enabled`: while a test runs, controls that would
// not reach the hardware are shown disabled (see client README, "Interlock and locked controls").

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
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = if (enabled) 1f else 0.4f)),
        colors = ButtonDefaults.buttonColors(containerColor = Color.Transparent,
            contentColor = MaterialTheme.colorScheme.onSurface,
            disabledContainerColor = MaterialTheme.colorScheme.surface,
            disabledContentColor = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.55f)),
        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 10.dp), content = content)
}

@Composable
fun OutlinedTextField(value: String, onValueChange: (String) -> Unit, modifier: Modifier = Modifier,
    label: @Composable (() -> Unit)? = null, singleLine: Boolean = true,
    keyboardOptions: KeyboardOptions = KeyboardOptions.Default, keyboardActions: KeyboardActions = KeyboardActions.Default,
    enabled: Boolean = true) {
    MaterialTextField(value, onValueChange, modifier, enabled = enabled, singleLine = singleLine,
        label = label, keyboardOptions = keyboardOptions, keyboardActions = keyboardActions, shape = RoundedCornerShape(3.dp),
        textStyle = MaterialTheme.typography.bodyLarge.copy(fontFamily = FontFamily.Monospace),
        colors = OutlinedTextFieldDefaults.colors(
            focusedContainerColor = MaterialTheme.colorScheme.background,
            unfocusedContainerColor = MaterialTheme.colorScheme.background,
            disabledContainerColor = MaterialTheme.colorScheme.surface,
            unfocusedBorderColor = MaterialTheme.colorScheme.outline,
            focusedBorderColor = MaterialTheme.colorScheme.primary))
}

/** A numeric text field. */
@Composable
fun NumberField(value: String, onValueChange: (String) -> Unit, label: String, modifier: Modifier = Modifier,
    enabled: Boolean = true, decimal: Boolean = false) {
    OutlinedTextField(value, onValueChange, modifier, label = { Text(label) }, enabled = enabled,
        keyboardOptions = KeyboardOptions(keyboardType = if (decimal) KeyboardType.Decimal else KeyboardType.Number))
}

@Composable
fun ToggleChip(label: String, checked: Boolean, onCheckedChange: (Boolean) -> Unit,
    enabled: Boolean = true, accent: Color = MaterialTheme.colorScheme.primary) {
    val shape = RoundedCornerShape(3.dp)
    val alpha = if (enabled) 1f else 0.45f
    Row(Modifier.heightIn(min = 48.dp).background(if (checked) MaterialTheme.colorScheme.surfaceVariant.copy(alpha = alpha) else Color.Transparent, shape)
        .border(1.dp, MaterialTheme.colorScheme.outline.copy(alpha = alpha), shape)
        .toggleable(checked, enabled, Role.Checkbox, onCheckedChange).padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        Box(Modifier.size(5.dp).background((if (checked) accent else MaterialTheme.colorScheme.outline).copy(alpha = alpha)))
        Text(label, style = MaterialTheme.typography.labelLarge,
            color = (if (checked && enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurfaceVariant).copy(alpha = alpha))
    }
}

@Composable
fun ChoiceChip(label: String, selected: Boolean, enabled: Boolean = true, onClick: () -> Unit) {
    val shape = RoundedCornerShape(3.dp)
    val alpha = if (enabled) 1f else 0.45f
    Box(Modifier.heightIn(min = 48.dp)
        .background((if (selected) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surface).copy(alpha = alpha), shape)
        .border(1.dp, (if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline).copy(alpha = alpha), shape)
        .selectable(selected, enabled = enabled, role = Role.RadioButton, onClick = onClick).padding(horizontal = 16.dp, vertical = 12.dp)) {
        Text(label, style = MaterialTheme.typography.labelLarge,
            color = (if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant).copy(alpha = alpha))
    }
}

/** 50 Ω / 75 Ω choice. */
@Composable
fun ImpedanceChoice(z: Impedance, enabled: Boolean = true, onSelect: (Impedance) -> Unit) {
    Impedance.entries.forEach { option -> ChoiceChip("${option.ohms} Ω", z == option, enabled) { onSelect(option) } }
}

/** Linear / logarithmic frequency spacing. */
@Composable
fun SpacingChoice(log: Boolean, enabled: Boolean = true, onSelect: (Boolean) -> Unit) {
    ChoiceChip("Lin", !log, enabled) { onSelect(false) }
    ChoiceChip("Log", log, enabled) { onSelect(true) }
}

/**
 * Relative output level (attenuator steps, 0.25 dB below maximum). [onChange] follows the drag;
 * [onCommit] fires once when the user lets go, which is where a live output gets its `set`.
 */
@Composable
fun LevelSlider(attenSteps: Int, onChange: (Int) -> Unit, modifier: Modifier = Modifier, enabled: Boolean = true,
    label: String? = null, onCommit: () -> Unit = {}) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        label?.let { Text(it, modifier = Modifier.width(112.dp), style = MaterialTheme.typography.bodyMedium) }
        Text(formatAtten(attenSteps), modifier = Modifier.width(84.dp), style = MaterialTheme.typography.bodyLarge,
            fontFamily = FontFamily.Monospace)
        Slider(value = attenSteps.toFloat(), onValueChange = { onChange(it.toInt()) }, onValueChangeFinished = onCommit,
            enabled = enabled, valueRange = ProtocolLimits.ATTEN_MIN.toFloat()..ProtocolLimits.ATTEN_MAX.toFloat(),
            steps = ProtocolLimits.ATTEN_MAX - ProtocolLimits.ATTEN_MIN - 1, modifier = Modifier.weight(1f),
            colors = SliderDefaults.colors(activeTrackColor = MaterialTheme.colorScheme.primary,
                inactiveTrackColor = MaterialTheme.colorScheme.outline,
                activeTickColor = Color.Transparent, inactiveTickColor = Color.Transparent))
    }
}

/** Quiet grouping for settings and measurements; spacing establishes the hierarchy. */
@Composable
fun Panel(title: String? = null, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Surface(modifier, shape = RoundedCornerShape(3.dp), color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant)) {
        Column(Modifier.padding(12.dp)) {
            title?.let {
                Text(it, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurface)
                Spacer(Modifier.height(8.dp))
            }
            content()
        }
    }
}

/** A "label ........ value" readout row. */
@Composable
fun LabeledValue(label: String, value: String, modifier: Modifier = Modifier) {
    Row(modifier.fillMaxWidth()) {
        Text(label, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
        Text(value, modifier = Modifier.weight(1f), fontFamily = FontFamily.Monospace,
            textAlign = TextAlign.End, color = MaterialTheme.colorScheme.onSurface)
    }
}

/** A message in the error color, with an optional test tag; nothing for null. */
@Composable
fun ErrorText(text: String?, tag: String? = null) {
    if (text == null) return
    Spacer(Modifier.height(6.dp))
    Text(text, color = MaterialTheme.colorScheme.error, modifier = if (tag != null) Modifier.testTag(tag) else Modifier)
}

/** Secondary explanation text. */
@Composable
fun Hint(text: String, modifier: Modifier = Modifier) {
    Text(text, modifier, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
}

/** A screen's scrolling column with its heading. */
@Composable
fun ScreenColumn(title: String, compactUi: Boolean, subtitle: String? = null, content: @Composable ColumnScope.() -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(if (compactUi) 8.dp else 14.dp)) {
        Text(title, style = MaterialTheme.typography.headlineSmall)
        subtitle?.let {
            Spacer(Modifier.height(4.dp))
            Text(it, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Spacer(Modifier.height(8.dp))
        content()
    }
}
