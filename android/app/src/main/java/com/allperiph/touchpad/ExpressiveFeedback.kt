package com.allperiph.touchpad

import androidx.compose.animation.core.*
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.PressInteraction
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch

/**
 * Material 3 Expressive 弹性触控反馈 —— Issue #15
 *
 * 为触控板/按键提供 Spring 动效反馈：
 * - 按下：scale 1.0 → 0.95 + elevation 下降
 * - 抬起：弹性回弹（overshoot）到 1.0
 * - 连接状态：morph 过渡
 * - 滚动：加速度曲线
 *
 * 用法：
 * ```kotlin
 * val state = rememberExpressivePressState()
 * Box(
 *     modifier = Modifier
 *         .expressivePress(state)
 *         .pointerInput(Unit) { detectTapGestures { ... } }
 * )
 * ```
 */

// —————— 1. 触控板按压弹性反馈 ——————

@Composable
fun rememberExpressivePressState(): ExpressivePressState {
    val scope = rememberCoroutineScope()
    return remember { ExpressivePressState(scope) }
}

class ExpressivePressState internal constructor(
    private val scope: kotlinx.coroutines.CoroutineScope
) {
    // scale: 按下时缩到 0.95，抬起时弹性回弹到 1.0
    val scale = Animatable(1f)

    // elevation: 按下时降低，抬起时回升
    val elevation = Animatable(1f)

    // 透明度反馈（按下时轻微变暗）
    val alpha = Animatable(1f)

    // 当前是否按下
    var isPressed by mutableStateOf(false)
        private set

    // Material 3 Expressive spring 参数
    private val pressSpring = spring<Float>(
        dampingRatio = 0.6f,      // 低阻尼 → 有弹性
        stiffness = StiffnessMediumLow
    )
    private val releaseSpring = spring<Float>(
        dampingRatio = 0.4f,      // 更低阻尼 → overshoot 回弹
        stiffness = StiffnessMedium
    )

    fun onPress() {
        isPressed = true
        scope.launch {
            launch { scale.animateTo(0.95f, pressSpring) }
            launch { elevation.animateTo(0.6f, pressSpring) }
            launch { alpha.animateTo(0.85f, tween(100)) }
        }
    }

    fun onRelease() {
        isPressed = false
        scope.launch {
            // 弹性回弹（overshoot）
            launch { scale.animateTo(1f, releaseSpring) }
            launch { elevation.animateTo(1f, releaseSpring) }
            launch { alpha.animateTo(1f, tween(150)) }
        }
    }

    // 取消（如手指滑出）
    fun onCancel() {
        isPressed = false
        scope.launch {
            scale.animateTo(1f, spring(dampingRatio = 0.5f, stiffness = StiffnessMedium))
            elevation.animateTo(1f, spring(dampingRatio = 0.5f))
            alpha.animateTo(1f, tween(200))
        }
    }
}

/**
 * 将弹性反馈应用到 Modifier
 */
fun Modifier.expressivePress(state: ExpressivePressState): Modifier {
    return this.then(
        Modifier.graphicsLayer {
            scaleX = state.scale.value
            scaleY = state.scale.value
            alpha = state.alpha.value
            // elevation 用 translationZ 模拟（Compose 原生 shadow）
            translationZ = state.elevation.value * 4.dp.toPx()
        }
    )
}

/**
 * 自动监听 press interaction（配合 clickable/pointerInput 使用）
 */
@Composable
fun Modifier.expressivePress(indication: MutableInteractionSource): Modifier {
    val state = rememberExpressivePressState()
    val scope = rememberCoroutineScope()

    LaunchedEffect(indication) {
        indication.interactions.collectLatest { interaction ->
            when (interaction) {
                is PressInteraction.Press -> state.onPress()
                is PressInteraction.Release -> state.onRelease()
                is PressInteraction.Cancel -> state.onCancel()
            }
        }
    }

    return expressivePress(state)
}

// —————— 2. 连接状态 morph 过渡 ——————

enum class ConnectionPhase {
    Idle, Discovering, Connecting, Connected, Failed
}

/**
 * 连接状态指示器：morph 动效过渡
 * Idle → Discovering: 渐变扩大 + 旋转
 * Discovering → Connecting: 形变
 * Connecting → Connected: 弹性弹入
 */
@Composable
fun ConnectionMorphIndicator(phase: ConnectionPhase) {
    val infiniteTransition = rememberInfiniteTransition(label = "conn")

    // 旋转动画（Discovering 时）
    val rotation by infiniteTransition.animateFloat(
        initialValue = 0f,
        targetValue = if (phase == ConnectionPhase.Discovering) 360f else 0f,
        animationSpec = infiniteRepeatable(
            animation = tween(1200, easing = LinearEasing),
            repeatMode = RepeatMode.Restart
        ),
        label = "rotation"
    )

    // 缩放（Connected 时弹性放大）
    val targetScale = when (phase) {
        ConnectionPhase.Idle -> 1f
        ConnectionPhase.Discovering -> 1.1f
        ConnectionPhase.Connecting -> 0.9f
        ConnectionPhase.Connected -> 1.3f
        ConnectionPhase.Failed -> 0.8f
    }
    val scale by animateFloatAsState(
        targetValue = targetScale,
        animationSpec = spring(
            dampingRatio = 0.5f,   // 弹性
            stiffness = StiffnessMediumLow
        ),
        label = "scale"
    )

    // 颜色
    val color = when (phase) {
        ConnectionPhase.Idle -> MaterialTheme.colorScheme.surfaceVariant
        ConnectionPhase.Discovering -> MaterialTheme.colorScheme.tertiary
        ConnectionPhase.Connecting -> MaterialTheme.colorScheme.secondary
        ConnectionPhase.Connected -> MaterialTheme.colorScheme.primary
        ConnectionPhase.Failed -> MaterialTheme.colorScheme.error
    }

    // 透明度
    val alpha by animateFloatAsState(
        targetValue = if (phase == ConnectionPhase.Idle) 0.5f else 1f,
        animationSpec = tween(300),
        label = "alpha"
    )

    Surface(
        modifier = Modifier
            .graphicsLayer {
                scaleX = scale
                scaleY = scale
                this.alpha = alpha
                rotationZ = rotation
            },
        shape = MaterialTheme.shapes.small,
        color = color
    ) {
        // 内容由调用方决定
    }
}

// —————— 3. 虚拟按键弹性反馈 ——————

/**
 * 虚拟按键：按下 scale + elevation 变化
 * 对应 HotkeyBoard / KeyboardPanels 的按键
 */
@Composable
fun ExpressiveKeyButton(
    label: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true
) {
    val state = rememberExpressivePressState()
    val interactionSource = remember { MutableInteractionSource() }

    LaunchedEffect(interactionSource) {
        interactionSource.interactions.collectLatest { interaction ->
            when (interaction) {
                is PressInteraction.Press -> state.onPress()
                is PressInteraction.Release -> state.onRelease()
                is PressInteraction.Cancel -> state.onCancel()
            }
        }
    }

    Surface(
        onClick = onClick,
        enabled = enabled,
        interactionSource = interactionSource,
        modifier = modifier
            .expressivePress(state),
        shape = MaterialTheme.shapes.small,
        color = if (state.isPressed)
            MaterialTheme.colorScheme.surfaceVariant
        else
            MaterialTheme.colorScheme.surface,
        tonalElevation = if (state.isPressed) 0.dp else 2.dp
    ) {
        // 按键内容
        androidx.compose.foundation.layout.Box(
            modifier = Modifier
                .androidx.compose.foundation.layout.padding(horizontal = 16.dp, vertical = 12.dp)
        ) {
            androidx.compose.material3.Text(text = label)
        }
    }
}

// —————— 4. 滚动惯性反馈 ——————

/**
 * 滚动加速度曲线：参考 Material 3 Expressive 的
 * emphasized-decelerate 曲线
 */
val EmphasizedDecelerate = CubicBezierEasing(0.05f, 0.7f, 0.1f, 1f)
val EmphasizedAccelerate = CubicBezierEasing(0.3f, 0f, 0.8f, 0.15f)

/**
 * 用于触控板滚动的惯性动画
 * 适用于将 delta 映射为滚动位置的场景
 */
fun scrollInertiaAnimation(
    initialVelocity: Float
): AnimationSpec<Float> {
    return spring(
        initialVelocity = initialVelocity,
        dampingRatio = 0.85f,      // 轻微阻尼（自然减速）
        stiffness = StiffnessMediumLow
    )
}