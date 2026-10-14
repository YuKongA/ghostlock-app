package com.ghostlock.app.ui.kit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import top.yukonga.miuix.kmp.basic.Button as MiuixButton
import top.yukonga.miuix.kmp.basic.Text as MiuixText

/**
 * 一个界面套件提供的组件面。
 *
 * 只列**临时界面真正用到**的三种：一排动作、一个动作按钮、一段正文。这不是最终形态 —— 生产 UI
 * 落地时按同一个形状扩展（KernelSU 的做法也是"每个屏幕两套实现、同一组语义"），而不是现在就把
 * 一整套组件都猜一遍。
 */
interface GhostlockKit {
    /** 套件名，显示在切换按钮上。 */
    val label: String

    /** 一排动作，水平排布。 */
    @Composable
    fun Actions(content: @Composable () -> Unit)

    /** 一个可点的动作。 */
    @Composable
    fun Action(label: String, onClick: () -> Unit)

    /** 一段正文（日志、说明）。 */
    @Composable
    fun Body(text: String, modifier: Modifier)
}

/** Material3 版的组件面。 */
object MaterialKit : GhostlockKit {
    override val label = "Material"

    @Composable
    override fun Actions(content: @Composable () -> Unit) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(4.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            content()
        }
    }

    @Composable
    override fun Action(label: String, onClick: () -> Unit) {
        Button(onClick = onClick) {
            Text(label)
        }
    }

    @Composable
    override fun Body(text: String, modifier: Modifier) {
        Text(text = text, modifier = modifier)
    }
}

/** Miuix 版的组件面：同样的语义，另一套控件。 */
object MiuixKit : GhostlockKit {
    override val label = "Miuix"

    @Composable
    override fun Actions(content: @Composable () -> Unit) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(4.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            content()
        }
    }

    @Composable
    override fun Action(label: String, onClick: () -> Unit) {
        MiuixButton(onClick = onClick) {
            MiuixText(text = label)
        }
    }

    @Composable
    override fun Body(text: String, modifier: Modifier) {
        MiuixText(text = text, modifier = modifier)
    }
}

/** 取一个套件的组件面。界面只调用它，不 import 具体控件。 */
fun kitOf(kit: UiKit): GhostlockKit = when (kit) {
    UiKit.Material -> MaterialKit
    UiKit.Miuix -> MiuixKit
}
