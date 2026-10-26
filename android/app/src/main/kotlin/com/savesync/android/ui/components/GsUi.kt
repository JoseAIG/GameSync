package com.savesync.android.ui.components

import android.content.Context
import android.content.res.Configuration
import android.hardware.input.InputManager
import android.view.InputDevice
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.savesync.android.ui.theme.GsColors
import androidx.compose.foundation.indication
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.material.ripple.rememberRipple
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ChevronRight
import androidx.compose.material3.Icon
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.max
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.runtime.State
import androidx.compose.ui.graphics.graphicsLayer
import com.savesync.android.ui.theme.GsElevation
import com.savesync.android.ui.theme.GsMotion
import com.savesync.android.ui.theme.LocalReduceMotion
import com.savesync.android.ui.theme.GsShapes
import com.savesync.android.ui.theme.LocalHighContrast
import com.savesync.android.ui.theme.gsDim
import com.savesync.android.ui.theme.gsTextMuted
import com.savesync.android.ui.theme.textVariantOf

// ── Controller glyphs ─────────────────────────────────────────────────────

/** A controller input as drawn in footer hints and dialogs. */
enum class GsButton(val label: String, val color: Color?, val round: Boolean) {
    A("A", GsColors.BtnA, true),
    B("B", GsColors.BtnB, true),
    X("X", GsColors.BtnX, true),
    Y("Y", GsColors.BtnY, true),
    L1("L1", null, false),
    R1("R1", null, false),
    L2R2("L2/R2", null, false),
    SELECT("SELECT", null, false),
    START("START", null, false),
    DPAD("D-pad", null, false),
}

/** Coloured round A/B/X/Y glyph, or a grey rounded-rect shoulder/menu glyph. */
@Composable
fun ButtonGlyph(button: GsButton, modifier: Modifier = Modifier) {
    if (button.round) {
        // Grows with the font scale so the letter never overflows the circle.
        val diameter = max(20.dp, with(LocalDensity.current) { (11.sp * 1.8f).toDp() })
        Box(
            modifier = modifier
                .size(diameter)
                .clip(CircleShape)
                .background(button.color ?: GsColors.PanelHi),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                button.label,
                color = GsColors.Ink,
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold,
            )
        }
    } else {
        Box(
            modifier = modifier
                .heightIn(min = 18.dp)
                .clip(RoundedCornerShape(5.dp))
                .background(GsColors.PanelHi)
                .border(1.dp, GsColors.Line, RoundedCornerShape(5.dp))
                .padding(horizontal = 6.dp, vertical = 1.dp),
            contentAlignment = Alignment.Center,
        ) {
            Text(
                button.label,
                color = GsColors.Dim,
                fontSize = 10.sp,
                fontWeight = FontWeight.Bold,
            )
        }
    }
}

data class GsHint(val button: GsButton, val label: String)

/**
 * Whether the controller footer is drawn.  Provided by MainApp: on when a
 * gamepad is attached (handhelds, Bluetooth pads) or the screen is in
 * landscape; off on a plain phone in portrait, where it would only waste
 * space under the user's thumb.
 */
val LocalShowButtonHints = compositionLocalOf { false }

/** True while any gamepad / joystick input device is attached. */
@Composable
fun rememberGamepadConnected(): Boolean {
    val context = LocalContext.current
    var connected by remember { mutableStateOf(hasGamepad()) }
    DisposableEffect(context) {
        val im = context.getSystemService(Context.INPUT_SERVICE) as? InputManager
        val listener = object : InputManager.InputDeviceListener {
            override fun onInputDeviceAdded(deviceId: Int) { connected = hasGamepad() }
            override fun onInputDeviceRemoved(deviceId: Int) { connected = hasGamepad() }
            override fun onInputDeviceChanged(deviceId: Int) { connected = hasGamepad() }
        }
        im?.registerInputDeviceListener(listener, null)
        connected = hasGamepad()
        onDispose { im?.unregisterInputDeviceListener(listener) }
    }
    return connected
}

/** Footer visibility rule — see [LocalShowButtonHints]. */
@Composable
fun rememberShowButtonHints(): Boolean {
    val gamepad = rememberGamepadConnected()
    val landscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    return gamepad || landscape
}

private fun hasGamepad(): Boolean = try {
    InputDevice.getDeviceIds().any { id ->
        val sources = InputDevice.getDevice(id)?.sources ?: 0
        (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }
} catch (_: Exception) {
    false
}

/**
 * The controller-hint footer: coloured A/B/X/Y glyphs plus a label each,
 * changing per screen.  Draws nothing when [LocalShowButtonHints] is off.
 */
@Composable
fun GsFooterHints(hints: List<GsHint>, modifier: Modifier = Modifier) {
    // Drawn once, as MainApp's Scaffold bottomBar (GsChrome.kt), so the
    // Scaffold already keeps the snackbar above it.
    if (!LocalShowButtonHints.current || hints.isEmpty()) return
    Column(
        modifier = modifier
            .fillMaxWidth()
            .background(GsColors.Bg2),
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(1.dp)
                .background(GsColors.Line)
        )
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState())
                .padding(horizontal = 12.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            hints.forEach { hint ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    ButtonGlyph(hint.button)
                    Spacer(Modifier.width(5.dp))
                    Text(
                        hint.label,
                        color = gsDim(),
                        style = MaterialTheme.typography.labelMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
    }
}

/**
 * Teal ring while the element (or something inside it) has focus, so
 * D-pad navigation through ordinary Material controls (Settings) is
 * visible on the dark palette.
 */
fun Modifier.gsFocusRing(shape: Shape = RoundedCornerShape(20.dp)): Modifier = composed {
    var focused by remember { mutableStateOf(false) }
    this
        .onFocusChanged { focused = it.isFocused || it.hasFocus }
        .border(2.dp, if (focused) GsColors.Accent2 else Color.Transparent, shape)
}

/**
 * A small visual (a tab pill, a filter chip) inside a touch target at least
 * 48×48dp, so it is easy to hit without looking bigger.  The ripple is drawn
 * on the visual: [visual] must apply the modifier it is given.
 */
@Composable
fun GsHitTarget(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    role: Role = Role.Button,
    visual: @Composable (indication: Modifier) -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    Box(
        modifier = modifier
            .heightIn(min = 48.dp)
            .widthIn(min = 48.dp)
            .clickable(interactionSource = source, indication = null, role = role, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        visual(Modifier.indication(source, rememberRipple()))
    }
}

// ── Rows, pills, banners ──────────────────────────────────────────────────

/**
 * A list row on a panel.  In Pad mode ([LocalInputMode]) the selected row
 * (gamepad cursor) is lifted to PANEL_HI with a teal selection bar down its
 * left edge, drawn instantly on the press that moved it.  In Touch mode
 * [isSelected] is ignored, so a phone doesn't show row 0 permanently
 * "selected".  Both modes show a press state — a handheld's screen is
 * touched in Pad mode too: PANEL_HI on press-down and a slight scale.
 */
@Composable
fun GsListRow(
    isSelected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    containerColor: Color = GsColors.Panel,
    content: @Composable RowScope.() -> Unit,
) {
    val shape = GsShapes.Row
    val selected = isSelected && LocalInputMode.current == GsInputMode.Pad
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale = rememberPressScale(pressed, GS_ROW_PRESS_SCALE)
    // High contrast: the selection border goes from Accent@55% to solid.
    val selectedBorder = if (LocalHighContrast.current) GsColors.Accent else GsColors.Accent.copy(alpha = 0.55f)
    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(IntrinsicSize.Min)
            .graphicsLayer {
                val s = scale.value
                scaleX = s
                scaleY = s
            }
            .clip(shape)
            .background(if (selected || pressed) GsColors.PanelHi else containerColor)
            .then(
                if (selected) Modifier.border(1.dp, selectedBorder, shape)
                else Modifier
            )
            .clickable(interactionSource = source, indication = rememberRipple(), onClick = onClick),
    ) {
        Box(
            modifier = Modifier
                .width(4.dp)
                .fillMaxHeight()
                .background(if (selected) GsColors.Accent else Color.Transparent)
        )
        Row(
            modifier = Modifier
                .weight(1f)
                .padding(horizontal = 12.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            content = content,
        )
    }
}

/** Press scale of a list row. */
const val GS_ROW_PRESS_SCALE = 0.98f
/** Press scale of a button (dialog choice, detail action). */
const val GS_BUTTON_PRESS_SCALE = 0.97f

/**
 * Scale for a pressable surface: down to [pressedScale] with the PRESS
 * spring on press-down, back with QUICK on release.  animateFloatAsState
 * re-targets with velocity, so a quick tap reverses mid-way instead of
 * restarting.  Always 1 under reduced motion.  Read the returned State only
 * inside `graphicsLayer {}` so the animation never recomposes.
 */
@Composable
fun rememberPressScale(pressed: Boolean, pressedScale: Float): State<Float> {
    val reduce = LocalReduceMotion.current
    return animateFloatAsState(
        targetValue = if (pressed && !reduce) pressedScale else 1f,
        animationSpec = if (pressed) GsMotion.press() else GsMotion.quick(),
        label = "pressScale",
    )
}

/**
 * Status pill: a 16% tint of [color] with its legible text variant
 * ([textVariantOf]), or a solid fill with ink text.  Under high contrast the
 * tint becomes an opaque PanelHi fill with a 1dp border in the text colour.
 */
@Composable
fun GsPill(
    text: String,
    color: Color,
    modifier: Modifier = Modifier,
    filled: Boolean = false,
    textColor: Color = textVariantOf(color),
) {
    val high = LocalHighContrast.current
    val shape = GsShapes.Pill
    Box(
        modifier = modifier
            .clip(shape)
            .background(
                when {
                    filled -> color
                    high -> GsColors.PanelHi
                    else -> color.copy(alpha = 0.16f)
                }
            )
            .then(if (high && !filled) Modifier.border(1.dp, textColor, shape) else Modifier)
            .padding(horizontal = 8.dp, vertical = 3.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = FontWeight.Bold,
            color = if (filled) GsColors.Ink else textColor,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * Status banner for an ongoing condition (offline, no server, conflicts):
 * a panel with a 4dp edge bar in [color] and text in its legible variant.
 * With [onClick] it is a button: 48dp minimum height and a trailing chevron.
 * See GsBanners.kt for which banners may show at once.
 */
@Composable
fun GsBanner(
    text: String,
    color: Color,
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
) {
    val high = LocalHighContrast.current
    val textColor = textVariantOf(color)
    val shape = GsShapes.Row
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 12.dp, vertical = 4.dp)
            .then(if (onClick != null) Modifier.heightIn(min = 48.dp) else Modifier)
            .height(IntrinsicSize.Min)
            .clip(shape)
            .background(GsColors.Panel)
            .then(if (high) Modifier.border(1.dp, textColor, shape) else Modifier)
            .then(
                if (onClick != null) Modifier.clickable(role = Role.Button, onClick = onClick)
                else Modifier
            ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            Modifier
                .width(4.dp)
                .fillMaxHeight()
                .background(color)
        )
        Text(
            text,
            color = textColor,
            style = MaterialTheme.typography.bodySmall,
            fontWeight = FontWeight.Medium,
            modifier = Modifier
                .weight(1f)
                .padding(horizontal = 12.dp, vertical = 8.dp),
        )
        if (onClick != null) {
            Icon(
                Icons.Filled.ChevronRight,
                contentDescription = null,
                tint = gsTextMuted(),
                modifier = Modifier
                    .padding(end = 12.dp)
                    .size(16.dp),
            )
        }
    }
}

// ── Dialogs ───────────────────────────────────────────────────────────────

/** One button of a [GsDialog]: pressed with [button] on a controller, or tapped. */
data class GsDialogChoice(
    val button: GsButton,
    val label: String,
    val onClick: () -> Unit,
    val color: Color = GsColors.PanelHi,
    val textColor: Color = GsColors.Text,
)

/**
 * Card dialog over the dimmed backdrop, driven by the controller like the
 * console clients: each choice answers to its own button glyph, B (and the
 * system Back gesture) cancels via [onDismiss] unless a choice claims B,
 * and START presses the A choice when [startConfirms].  Touch users get the
 * same choices as buttons.
 *
 * A Dialog is its own window, so its keys never reach MainActivity's
 * dispatchKeyEvent or the screen behind it — they are handled here.
 *
 * Input safety: the choice buttons never take D-pad focus (so a focus ring
 * can't sit on "Cancel" while A runs "Delete"), key repeats of a held
 * A/X/Y/START/Enter are ignored (a held A can't open and confirm in one
 * press), and the D-pad is consumed so focus can't wander.  A body with its
 * own cursor passes [onNavigate], which receives Up/Down as -1/+1; it draws
 * its rows with `GsListRow(isSelected = …)` and brings the selected row into
 * view itself.
 */
@Composable
fun GsDialog(
    title: String,
    onDismiss: () -> Unit,
    choices: List<GsDialogChoice>,
    message: String = "",
    startConfirms: Boolean = false,
    onNavigate: ((dy: Int) -> Unit)? = null,
    body: (@Composable ColumnScope.() -> Unit)? = null,
) {
    val focusRequester = remember { FocusRequester() }
    fun choiceFor(button: GsButton): GsDialogChoice? = choices.firstOrNull { it.button == button }
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            shape = GsShapes.Dialog,
            color = GsColors.Panel,
            shadowElevation = GsElevation.DialogShadow,
            tonalElevation = GsElevation.DialogTonal,
            modifier = Modifier
                .widthIn(min = 280.dp, max = 520.dp)
                .border(1.dp, GsColors.Line, GsShapes.Dialog)
                .focusRequester(focusRequester)
                .focusable()
                .onPreviewKeyEvent { event ->
                    if (event.type != KeyEventType.KeyDown) {
                        return@onPreviewKeyEvent false
                    }
                    val repeat = event.nativeKeyEvent.repeatCount > 0
                    when (event.key) {
                        Key.ButtonA, Key.Enter, Key.NumPadEnter, Key.DirectionCenter -> {
                            if (!repeat) choiceFor(GsButton.A)?.onClick?.invoke()
                            true
                        }
                        Key.ButtonX -> {
                            if (!repeat) choiceFor(GsButton.X)?.onClick?.invoke()
                            true
                        }
                        Key.ButtonY -> {
                            if (!repeat) choiceFor(GsButton.Y)?.onClick?.invoke()
                            true
                        }
                        Key.ButtonStart -> {
                            if (!repeat && startConfirms) choiceFor(GsButton.A)?.onClick?.invoke()
                            true
                        }
                        Key.ButtonB, Key.Escape -> {
                            val b = choiceFor(GsButton.B)
                            if (b != null) b.onClick() else onDismiss()
                            true
                        }
                        Key.DirectionUp -> { onNavigate?.invoke(-1); true }
                        Key.DirectionDown -> { onNavigate?.invoke(1); true }
                        // Nothing in a dialog moves sideways; keep focus put.
                        Key.DirectionLeft, Key.DirectionRight -> true
                        else -> false
                    }
                },
        ) {
            Column(modifier = Modifier.padding(20.dp)) {
                Text(
                    title,
                    color = GsColors.Text,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
                if (message.isNotBlank()) {
                    Spacer(Modifier.height(8.dp))
                    Text(
                        message,
                        color = gsDim(),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
                if (body != null) {
                    Spacer(Modifier.height(8.dp))
                    Column(
                        modifier = Modifier
                            .heightIn(max = 360.dp)
                            .verticalScroll(rememberScrollState()),
                        content = body,
                    )
                }
                Spacer(Modifier.height(18.dp))
                FlowRowChoices(choices)
            }
        }
        // Inside the Dialog's own composition: the requester lives there.
        LaunchedEffect(Unit) { runCatching { focusRequester.requestFocus() } }
    }
}

/** Choices right-aligned, wrapping onto a second line on narrow phones. */
@Composable
private fun FlowRowChoices(choices: List<GsDialogChoice>) {
    // Ordered B (cancel) first, A (confirm) last — the console layout.
    val ordered = choices.sortedBy {
        when (it.button) {
            GsButton.B -> 0
            GsButton.Y -> 1
            GsButton.X -> 2
            GsButton.A -> 4
            else -> 3
        }
    }
    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.End,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        ordered.chunked(3).forEach { line ->
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                line.forEach { choice ->
                    DialogChoice(choice.button, choice.label, choice.color, choice.textColor, choice.onClick)
                }
            }
        }
    }
}

/** Yes/no [GsDialog]: A (or START when [startConfirms]) confirms, B cancels. */
@Composable
fun GsConfirmDialog(
    title: String,
    message: String,
    confirmLabel: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
    dismissLabel: String = "Cancel",
    startConfirms: Boolean = false,
    destructive: Boolean = false,
    /** An irreversibility line ("This can't be undone."), drawn in ErrText. */
    warning: String? = null,
) {
    GsDialog(
        title = title,
        message = message,
        onDismiss = onDismiss,
        startConfirms = startConfirms,
        body = warning?.let { w ->
            {
                Text(
                    w,
                    color = GsColors.ErrText,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                )
            }
        },
        choices = listOf(
            GsDialogChoice(GsButton.B, dismissLabel, onDismiss),
            GsDialogChoice(
                GsButton.A,
                confirmLabel,
                onConfirm,
                color = if (destructive) GsColors.Err else GsColors.Accent,
                textColor = GsColors.Ink,
            ),
        ),
    )
}

@Composable
private fun DialogChoice(
    button: GsButton,
    label: String,
    background: Color,
    textColor: Color,
    onClick: () -> Unit,
) {
    val source = remember { MutableInteractionSource() }
    val pressed by source.collectIsPressedAsState()
    val scale = rememberPressScale(pressed, GS_BUTTON_PRESS_SCALE)
    Row(
        modifier = Modifier
            // Pressed by its button or tapped, never focused by the D-pad:
            // canFocus only affects key focus, not touch or TalkBack.
            .focusProperties { canFocus = false }
            .heightIn(min = 48.dp)
            .graphicsLayer {
                val s = scale.value
                scaleX = s
                scaleY = s
            }
            .clip(GsShapes.Choice)
            .background(background)
            .clickable(interactionSource = source, indication = rememberRipple(), onClick = onClick)
            .padding(horizontal = 14.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        ButtonGlyph(button)
        Spacer(Modifier.width(8.dp))
        Text(
            label,
            color = textColor,
            fontWeight = FontWeight.SemiBold,
            textAlign = TextAlign.Center,
        )
    }
}
