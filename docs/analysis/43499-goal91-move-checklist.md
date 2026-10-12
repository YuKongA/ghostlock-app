# 43499 · 第 91 轮：搬移清单（**现行行号** + 内容锚点，双重可查）✓✓

## 一、更正：行号已漂移（✗，必须记 ✓）

```
第 29/89 轮记的"`:1143-1167`"等**绝对行号已失效** ✗（⇒ 文件随后续编辑增长 ✓）；
经查现行 `:1143` 已是 `stage_context.window = &window_guard;` ✗ ⇒ ⇒ **⇒ 以后只用*内容锚点*** ✓。
```

## 二、搬移清单（✓✓，现行行号 ✓）

| 角色 | 现行位置 | 内容锚点（更稳 ✓） |
| --- | --- | --- |
| **目标位置**（主路由） | `:1164` | `stage_refusal = cve49::steps::run_window_stage_with_attempt(` ✓ |
| **提前点**（修复的 prepare） | `:1217` | `if (!cve49::steps::run_prepare(prepare, prepared2, &repair_refusal) \|\|` ✓ |
| 建页 | `:1233-1241` | `build_payload_page(repair_image.data(), …)` ✓ + `memcpy(… kOrder3 …)` ✓ |
| 页槽入/出 | `:1244-1249` | `repair_slot.stash(…)` / `repair_slot.activate(…)` ✓（第 30 轮已接 ✓） |
| **发送**（已用槽 ✓） | `:1251` | `send_bytes(prepared2.shaping_fd, repair_page, repair_length)` ✓ |

```
⇒ ⇒ **⇒ 搬移范围 = `:1215-1249`（prepare + 建页 + 入槽）⇒ 上移到 `:1164` 之前** ✓；
⇒ 而**发送点已在用槽** ✓（第 30 轮 ✓）⇒ ⇒ **⇒ 这确实只是一次搬移** ✓（代码不增不减 ✓）。
```

## 三、执行时的三件事（✓，一次做完 ✓）

```
① 搬移（上述范围 ⇒ 主路由之前 ✓）；② 该次路由的延迟键改为 5000 µs ✓（第 90 轮：取值走 profile/wire ✓）；
③ 出口全覆盖 `discard` ✓（守则 §5 ✓）；⇒ 门禁三线**同批**跑 ✓（含 host 构建 ✓ —— 第 85 轮教训 ✓）。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
