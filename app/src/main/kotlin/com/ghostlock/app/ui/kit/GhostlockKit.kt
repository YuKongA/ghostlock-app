package com.ghostlock.app.ui.kit

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Column as LayoutColumn
import androidx.compose.foundation.layout.paddingFromBaseline
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material3.Button
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.TopAppBar
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationRail
import androidx.compose.material3.NavigationRailItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.Card as M3Card
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import top.yukonga.miuix.kmp.basic.Button as MiuixButton
import top.yukonga.miuix.kmp.basic.NavigationBar as MiuixNavigationBar
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.SmallTopAppBar
import top.yukonga.miuix.kmp.basic.NavigationBarItem as MiuixNavigationBarItem
import top.yukonga.miuix.kmp.basic.Card as MiuixCard
import top.yukonga.miuix.kmp.basic.Switch as MiuixSwitch
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** 间距 token（M3 的 8dp 栅格：4 只用于行内间隙，其余都是 8 的倍数）。 */
object Spacing {
    val xs = 4.dp
    val sm = 8.dp
    val md = 12.dp
    val lg = 16.dp
    val xl = 24.dp
}

/** 底栏的一个目的地：标签 + 图标（两套都用它，形状与 M3 的 NavigationBarItem 对齐）。 */
data class NavItem(val label: String, val icon: ImageVector)

/**
 *  一个界面套件提供的**语义**组件面（2026-10-17 正式 UI）：
 *
 *  - 只按“设置项 / 段落 / 按钮”这类语义定义，不按底层控件定义；
 *  - 页面代码只调用本接口，不 import Material 或 miuix；
 *  - 两套实现一一对应（Material 用 ListItem 组合，miuix 用 Switch/Card/Button）。
 *
 *  临时界面（tempui）用的旧三件（Actions/Action/Body）继续保留，避免破坏现有按钮。
 */
interface GhostlockKit {
    val label: String

    @Composable fun Actions(content: @Composable () -> Unit)
    @Composable fun Action(label: String, onClick: () -> Unit)
    @Composable fun Body(text: String, modifier: Modifier)

    /** 一页：标题 + 内容（两套都只是 Column，但标题字号/间距由套件决定）。 */
    @Composable fun Screen(title: String, content: @Composable () -> Unit)

    /** 一段：可空标题 + 卡片容器。 */
    @Composable fun Section(title: String?, content: @Composable () -> Unit)

    /** 开关项。 */
    @Composable fun SwitchItem(title: String, summary: String?, checked: Boolean, onChange: (Boolean) -> Unit)

    /** 箭头项（点开下一页 / 弹选择）。 */
    @Composable fun ArrowItem(title: String, summary: String?, onClick: () -> Unit)

    /** 下拉项（单选，就地展开）。 */
    @Composable fun DropdownItem(title: String, items: List<String>, selected: Int, onSelect: (Int) -> Unit)

    /** 单选行（用于列表选择，例如路径清单）。 */
    @Composable fun RadioItem(title: String, summary: String?, selected: Boolean, enabled: Boolean, onClick: () -> Unit)

    /** 主按钮（执行）。 */
    @Composable fun PrimaryButton(text: String, enabled: Boolean, onClick: () -> Unit)

    /** 对话框（标题 + 内容 + 关闭）。 */
    @Composable fun Dialog(title: String, content: @Composable () -> Unit, onDismiss: () -> Unit)

    /**
     *  状态头（KernelSU 式）：最上面一张卡 —— 大字状态 + 一行副标题。
     *
     *  @param title    状态主文（如“工作中/已就绪/受限”）。
     *  @param subtitle 一行细节（版本 / 内核 / 权限）。
     *  @param active   true = 正常（用 primary 容器色），false = 异常/受限（用 error 容器色）。
     */
    @Composable fun Hero(title: String, subtitle: String, active: Boolean)

    /** 顶部提示条（KernelSU 的黄色/红色 notice 卡）：一行文字 + 语义色。 */
    @Composable fun Banner(text: String, warn: Boolean = true)

    /** 顶栏（M3 TopAppBar 语义）：标题 + 可选动作。 */
    @Composable fun TopBar(title: String, actions: @Composable () -> Unit = {})

    /** 次按钮（M3 filled tonal）：用于“换路径重试”这类非主操作。 */
    @Composable fun SecondaryButton(text: String, enabled: Boolean, onClick: () -> Unit)

    /** 文本动作（M3 text button）。 */
    @Composable fun TextAction(text: String, onClick: () -> Unit)

    /** 底栏：主页 / 路径 / 日志 / 设置（语义：一组选项卡 + 当前项）。 */
    @Composable fun Navigation(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit)

    /** 宽屏导航栏（M3 NavigationRail）；窄屏仍用 [Navigation]。 */
    @Composable fun NavigationRail(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit)

    /** Scaffold：顶栏 + 底栏 + 内容（内容自动避开系统栏）。 */
    @Composable fun Page(
        topBar: @Composable () -> Unit,
        bottomBar: @Composable () -> Unit,
        content: @Composable () -> Unit,
    )
}

/** Material3 版的组件面。 */
object MaterialKit : GhostlockKit {
    override val label = "Material"

    @Composable override fun Actions(content: @Composable () -> Unit) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = Spacing.lg, vertical = Spacing.xs),
            horizontalArrangement = Arrangement.spacedBy(Spacing.sm),
        ) { content() }
    }

    @Composable override fun Action(label: String, onClick: () -> Unit) { Button(onClick = onClick) { Text(label) } }

    @Composable override fun Body(text: String, modifier: Modifier) {
        Text(text = text, modifier = modifier, style = MaterialTheme.typography.bodyMedium)
    }

    @Composable override fun Banner(text: String, warn: Boolean) {
        val colors = if (warn) {
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.tertiaryContainer,
                contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
            )
        } else {
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.errorContainer,
                contentColor = MaterialTheme.colorScheme.onErrorContainer,
            )
        }
        Card(
            modifier = Modifier.fillMaxWidth().padding(vertical = Spacing.xs),
            shape = RoundedCornerShape(16.dp),
            colors = colors,
        ) {
            Text(
                text = text,
                modifier = Modifier.padding(Spacing.md),
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable override fun Hero(title: String, subtitle: String, active: Boolean) {
        val colors = if (active) {
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.primaryContainer,
                contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
            )
        } else {
            CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.errorContainer,
                contentColor = MaterialTheme.colorScheme.onErrorContainer,
            )
        }
        M3Card(Modifier.fillMaxWidth().padding(vertical = Spacing.xs), colors = colors) {
            Column(Modifier.padding(Spacing.lg)) {
                Text(text = title, style = MaterialTheme.typography.headlineSmall)
                Spacer(Modifier.height(Spacing.xs))
                Text(text = subtitle, style = MaterialTheme.typography.bodyMedium)
            }
        }
    }

    @OptIn(ExperimentalMaterial3Api::class)
    @Composable override fun TopBar(title: String, actions: @Composable () -> Unit) {
        TopAppBar(
            title = { Text(text = title, style = MaterialTheme.typography.titleLarge) },
            actions = { actions() },
            colors = TopAppBarDefaults.topAppBarColors(),
        )
    }

    @Composable override fun Screen(title: String, content: @Composable () -> Unit) {
        Column(Modifier.fillMaxWidth().padding(horizontal = Spacing.lg, vertical = Spacing.sm)) {
            if (title.isNotEmpty()) {
                Text(text = title, style = MaterialTheme.typography.headlineSmall)
                Spacer(Modifier.height(Spacing.sm))
            }
            content()
        }
    }

    @Composable override fun Section(title: String?, content: @Composable () -> Unit) {
        Card(
            modifier = Modifier.fillMaxWidth().padding(vertical = Spacing.xs),
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(
                containerColor = MaterialTheme.colorScheme.surfaceContainer,
            ),
        ) {
            Column(Modifier.padding(Spacing.md)) {
                if (title != null) {
                    Text(text = title, style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(Spacing.xs))
                }
                content()
            }
        }
    }

    @Composable override fun SwitchItem(title: String, summary: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
        /* M3 ListItem：标题/副标题 + 尾部开关（行高与留白由组件给，不再手拼 Row）。 */
        ListItem(
            headlineContent = { Text(text = title, style = MaterialTheme.typography.bodyLarge) },
            supportingContent = summary?.let {
                { Text(text = it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            },
            trailingContent = { Switch(checked = checked, onCheckedChange = onChange) },
        )
    }

    @Composable override fun ArrowItem(title: String, summary: String?, onClick: () -> Unit) {
        ListItem(
            headlineContent = { Text(text = title, style = MaterialTheme.typography.bodyLarge) },
            supportingContent = summary?.let {
                { Text(text = it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            },
            trailingContent = {
                Icon(
                    imageVector = androidx.compose.material.icons.Icons.AutoMirrored.Filled.KeyboardArrowRight,
                    contentDescription = null,
                )
            },
            modifier = Modifier.clickable(onClick = onClick),
        )
    }

    @Composable override fun DropdownItem(title: String, items: List<String>, selected: Int, onSelect: (Int) -> Unit) {
        var open by remember { mutableStateOf(false) }
        Row(Modifier.fillMaxWidth().clickable { open = true }.padding(vertical = Spacing.sm), verticalAlignment = Alignment.CenterVertically) {
            Text(title, Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
            Text(items.getOrElse(selected) { "-" }, style = MaterialTheme.typography.bodyMedium)
            DropdownMenu(expanded = open, onDismissRequest = { open = false }) {
                items.forEachIndexed { index, item ->
                    DropdownMenuItem(text = { Text(item) }, onClick = { onSelect(index); open = false })
                }
            }
        }
    }

    @Composable override fun RadioItem(title: String, summary: String?, selected: Boolean, enabled: Boolean, onClick: () -> Unit) {
        ListItem(
            headlineContent = { Text(text = title, style = MaterialTheme.typography.bodyLarge) },
            supportingContent = summary?.let {
                { Text(text = it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant) }
            },
            leadingContent = { RadioButton(selected = selected, onClick = onClick, enabled = enabled) },
            modifier = Modifier.clickable(enabled = enabled, onClick = onClick),
        )
    }

    @Composable override fun PrimaryButton(text: String, enabled: Boolean, onClick: () -> Unit) {
        Button(
            onClick = onClick,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().padding(top = Spacing.sm).heightIn(min = 40.dp),
        ) {
            Text(text = text, style = MaterialTheme.typography.labelLarge)
        }
    }

    @Composable override fun SecondaryButton(text: String, enabled: Boolean, onClick: () -> Unit) {
        FilledTonalButton(
            onClick = onClick,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().padding(top = Spacing.sm).heightIn(min = 40.dp),
        ) {
            Text(text = text, style = MaterialTheme.typography.labelLarge)
        }
    }

    @Composable override fun TextAction(text: String, onClick: () -> Unit) {
        TextButton(onClick = onClick) { Text(text = text, style = MaterialTheme.typography.labelLarge) }
    }

    @Composable override fun Dialog(title: String, content: @Composable () -> Unit, onDismiss: () -> Unit) {
        AlertDialog(
            onDismissRequest = onDismiss,
            confirmButton = { TextButton(onClick = onDismiss) { Text("关闭") } },
            title = { Text(text = title, style = MaterialTheme.typography.titleLarge) },
            text = { content() },
        )
    }

    @Composable override fun NavigationRail(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit) {
        NavigationRail {
            items.forEachIndexed { index, item ->
                NavigationRailItem(
                    selected = index == selected,
                    onClick = { onSelect(index) },
                    icon = { Icon(item.icon, contentDescription = item.label) },
                    label = { Text(item.label) },
                )
            }
        }
    }

    @Composable override fun Page(
        topBar: @Composable () -> Unit,
        bottomBar: @Composable () -> Unit,
        content: @Composable () -> Unit,
    ) {
        Column(Modifier.fillMaxSize()) {
            topBar()
            Box(Modifier.weight(1f)) { content() }
            bottomBar()
        }
    }

    @Composable override fun Navigation(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit) {
        /* M3 NavigationBar：图标 + 标签，选中态由组件自己画（不再用按钮拼）。 */
        NavigationBar {
            items.forEachIndexed { index, item ->
                NavigationBarItem(
                    selected = index == selected,
                    onClick = { onSelect(index) },
                    icon = { Icon(item.icon, contentDescription = item.label) },
                    label = { Text(item.label) },
                )
            }
        }
    }
}

/** miuix 版的组件面：同样的语义，另一套控件。 */
object MiuixKit : GhostlockKit {
    override val label = "Miuix"

    @Composable override fun Actions(content: @Composable () -> Unit) {
        Row(
            Modifier.fillMaxWidth().padding(horizontal = Spacing.lg, vertical = Spacing.xs),
            horizontalArrangement = Arrangement.spacedBy(Spacing.sm),
        ) { content() }
    }

    @Composable override fun Action(label: String, onClick: () -> Unit) { MiuixButton(onClick = onClick) { MiuixText(text = label) } }

    @Composable override fun Body(text: String, modifier: Modifier) {
        MiuixText(text = text, modifier = modifier, style = MiuixTheme.textStyles.body1)
    }

    @Composable override fun Banner(text: String, warn: Boolean) {
        MiuixCard(Modifier.fillMaxWidth().padding(vertical = Spacing.xs)) {
            Column(Modifier.padding(Spacing.md)) {
                MiuixText(text = text, style = MiuixTheme.textStyles.footnote1)
            }
        }
    }

    @Composable override fun Hero(title: String, subtitle: String, active: Boolean) {
        MiuixCard(Modifier.fillMaxWidth().padding(vertical = Spacing.xs)) {
            Column(Modifier.padding(Spacing.lg)) {
                MiuixText(text = title, style = MiuixTheme.textStyles.title1)
                Spacer(Modifier.height(Spacing.xs))
                MiuixText(
                    text = subtitle,
                    style = MiuixTheme.textStyles.footnote1,
                )
            }
        }
    }

    @Composable override fun TopBar(title: String, actions: @Composable () -> Unit) {
        SmallTopAppBar(title = title, actions = { actions() })
    }

    @Composable override fun Screen(title: String, content: @Composable () -> Unit) {
        Column(Modifier.fillMaxWidth().padding(horizontal = Spacing.lg, vertical = Spacing.sm)) {
            if (title.isNotEmpty()) {
                MiuixText(text = title, style = MiuixTheme.textStyles.title1)
                Spacer(Modifier.height(Spacing.sm))
            }
            content()
        }
    }

    @Composable override fun Section(title: String?, content: @Composable () -> Unit) {
        MiuixCard(Modifier.fillMaxWidth().padding(vertical = Spacing.xs)) {
            Column(Modifier.padding(Spacing.md)) {
                if (title != null) {
                    MiuixText(text = title, style = MiuixTheme.textStyles.title3)
                    Spacer(Modifier.height(Spacing.xs))
                }
                content()
            }
        }
    }

    @Composable override fun SwitchItem(title: String, summary: String?, checked: Boolean, onChange: (Boolean) -> Unit) {
        Row(Modifier.fillMaxWidth().padding(vertical = Spacing.sm), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                MiuixText(text = title, style = MiuixTheme.textStyles.body1)
                if (summary != null) MiuixText(text = summary, style = MiuixTheme.textStyles.footnote1)
            }
            MiuixSwitch(checked = checked, onCheckedChange = onChange)
        }
    }

    @Composable override fun ArrowItem(title: String, summary: String?, onClick: () -> Unit) {
        Row(Modifier.fillMaxWidth().clickable(onClick = onClick).padding(vertical = Spacing.sm), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                MiuixText(text = title, style = MiuixTheme.textStyles.body1)
                if (summary != null) MiuixText(text = summary, style = MiuixTheme.textStyles.footnote1)
            }
            MiuixText(text = "›")
        }
    }

    /** miuix 的下拉就地循环（该版本没有稳定的 Dropdown 公共签名；语义相同：点一次换下一项）。 */
    @Composable override fun DropdownItem(title: String, items: List<String>, selected: Int, onSelect: (Int) -> Unit) {
        Row(Modifier.fillMaxWidth().clickable { if (items.isNotEmpty()) onSelect((selected + 1) % items.size) }.padding(vertical = Spacing.sm), verticalAlignment = Alignment.CenterVertically) {
            MiuixText(text = title, modifier = Modifier.weight(1f), style = MiuixTheme.textStyles.body1)
            MiuixText(text = items.getOrElse(selected) { "-" }, style = MiuixTheme.textStyles.body2)
        }
    }

    @Composable override fun RadioItem(title: String, summary: String?, selected: Boolean, enabled: Boolean, onClick: () -> Unit) {
        Row(Modifier.fillMaxWidth().clickable(enabled = enabled, onClick = onClick).padding(vertical = Spacing.sm), verticalAlignment = Alignment.CenterVertically) {
            MiuixText(text = if (selected) "●" else "○")
            Column(Modifier.weight(1f)) {
                MiuixText(text = title, style = MiuixTheme.textStyles.body1)
                if (summary != null) MiuixText(text = summary, style = MiuixTheme.textStyles.footnote1)
            }
        }
    }

    @Composable override fun PrimaryButton(text: String, enabled: Boolean, onClick: () -> Unit) {
        MiuixButton(
            onClick = onClick,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().padding(top = Spacing.sm).heightIn(min = 40.dp),
        ) {
            MiuixText(text = text, style = MiuixTheme.textStyles.button)
        }
    }

    @Composable override fun SecondaryButton(text: String, enabled: Boolean, onClick: () -> Unit) {
        MiuixButton(
            onClick = onClick,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().padding(top = Spacing.sm).heightIn(min = 40.dp),
        ) {
            MiuixText(text = text, style = MiuixTheme.textStyles.button)
        }
    }

    @Composable override fun TextAction(text: String, onClick: () -> Unit) {
        MiuixText(
            text = text,
            modifier = Modifier.clickable(onClick = onClick).padding(Spacing.sm),
            style = MiuixTheme.textStyles.button,
        )
    }

    @Composable override fun Dialog(title: String, content: @Composable () -> Unit, onDismiss: () -> Unit) {
        Dialog(onDismissRequest = onDismiss, properties = DialogProperties()) {
            MiuixCard {
                Column(Modifier.padding(12.dp)) { MiuixText(text = title); content() }
            }
        }
    }

    @Composable override fun NavigationRail(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit) {
        LayoutColumn(Modifier.fillMaxWidth().padding(Spacing.sm)) {
            items.forEachIndexed { index, item ->
                MiuixText(
                    text = (if (index == selected) "● " else "○ ") + item.label,
                    modifier = Modifier.clickable { onSelect(index) }.padding(Spacing.sm),
                    style = MiuixTheme.textStyles.body1,
                )
            }
        }
    }

    @Composable override fun Page(
        topBar: @Composable () -> Unit,
        bottomBar: @Composable () -> Unit,
        content: @Composable () -> Unit,
    ) {
        Column(Modifier.fillMaxSize()) {
            topBar()
            Box(Modifier.weight(1f)) { content() }
            bottomBar()
        }
    }

    @Composable override fun Navigation(items: List<NavItem>, selected: Int, onSelect: (Int) -> Unit) {
        /* miuix 自带的 NavigationBar（IconAndText 模式），语义与 M3 一一对应。 */
        MiuixNavigationBar {
            items.forEachIndexed { index, item ->
                MiuixNavigationBarItem(
                    selected = index == selected,
                    onClick = { onSelect(index) },
                    icon = item.icon,
                    label = item.label,
                )
            }
        }
    }
}

/** 取一个套件的组件面。界面只调用它，不 import 具体控件。 */
fun kitOf(kit: UiKit): GhostlockKit = when (kit) {
    UiKit.Material -> MaterialKit
    UiKit.Miuix -> MiuixKit
}
