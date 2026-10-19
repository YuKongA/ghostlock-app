package com.ghostlock.app.ui.kit

import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview
import com.ghostlock.app.core.model.OptionSpec

/**
 *  把选项声明渲染成控件（2026-10-17 正式 UI 的核心：**加路径不用写 UI**）。
 *
 *  映射：
 *  - Toggle  → SwitchItem
 *  - Single  → DropdownItem（点一次换下一项 / 弹出列表）
 *  - Multi   → 每个候选项一个 SwitchItem
 *  - IntRange→ ArrowItem，点一次 +step，到顶回绕
 *  - Text    → ArrowItem 展示当前值（编辑弹窗下一步接）
 *
 *  @param specs    选项声明（路径/收尾自己给的）。
 *  @param values   当前值（缺省用 spec 的 default）。
 *  @param onChange 值变化回调（key, 新值）。
 *  @param kit      组件面（默认取 [LocalUiKit]）。
 */
@Composable
fun OptionList(
    specs: List<OptionSpec>,
    values: Map<String, Any>,
    onChange: (String, Any) -> Unit,
    kit: GhostlockKit = LocalUiKit.current,
) {
    for (spec in specs) {
        when (spec) {
            is OptionSpec.Toggle -> kit.SwitchItem(
                title = spec.title,
                summary = spec.summary,
                checked = values[spec.key] as? Boolean ?: spec.default,
                onChange = { onChange(spec.key, it) },
            )

            is OptionSpec.Single -> kit.DropdownItem(
                title = spec.title,
                items = spec.items,
                selected = values[spec.key] as? Int ?: spec.defaultIndex,
                onSelect = { onChange(spec.key, it) },
            )

            is OptionSpec.Multi -> {
                val selected = values[spec.key] as? Set<*> ?: spec.defaultSelected
                spec.items.forEachIndexed { index, item ->
                    kit.SwitchItem(
                        title = item,
                        summary = spec.summary,
                        checked = selected.contains(index),
                        onChange = { on ->
                            val next = selected.mapNotNull { it as? Int }.toMutableSet()
                            if (on) next += index else next -= index
                            onChange(spec.key, next)
                        },
                    )
                }
            }

            is OptionSpec.IntRange -> {
                val current = values[spec.key] as? Int ?: spec.default
                kit.ArrowItem(
                    title = spec.title,
                    summary = (spec.summary?.plus(" · ") ?: "") + current,
                    onClick = {
                        val next = if (current + spec.step > spec.max) spec.min else current + spec.step
                        onChange(spec.key, next)
                    },
                )
            }

            is OptionSpec.Text -> kit.ArrowItem(
                title = spec.title,
                summary = values[spec.key] as? String ?: spec.default,
                onClick = { },
            )
        }
    }
}
