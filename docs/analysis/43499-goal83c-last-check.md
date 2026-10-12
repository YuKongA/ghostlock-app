# 43499 · 第 83 轮（终）：第二处修复的可见性 ⇒ 剩一个待查点 ✓

## 一、已确定的（✓✓）

```
① `main: repair target=…`（78 次同值 ✓）来自 **`:836-843`** ✓ ⇒ 目标**写死** `init_cred+8` ✓（W2 专用 ✓）；
② 第二处（用 `repair_target` 变量 ✓，含 w1 的 `base + buffer_size` ✓）在 `:1143-1207` ✓，
   其 printf 只有三类：`route status=…` ✓、`sentinel-after2` ✓、**`repair-prepare-refused`（仅失败 ✓）**；
真机：`repair-prepare-refused` 计数 **0** ✓ ⇒ ⇒ ② **要么"成功但无声" ✓，要么"不在 w1 的执行路径上"** ✗；
`buffer_size` 已证非零 ✓（`route_config.cpp:46`：缺则必具名拒绝 ✓，而真机无该拒绝 ✓）。
```

## 二、最后一个待查点（✓，一处读 ✓）

```
**② 是否在 `step == "w1"` 的执行路径上** ✗ —— 即：`if (repair_target != 0U)` 那段所在的代码块
是否**被 step 分支包含** ✓（⇒ 若它只在 w2 的块里 ✗ ⇒ w1 永远不做修复 ✓ ⇒ ⇒ **⇒ 那就是 W1b 缺失的实因** ✓✓）。
⇒ 一并补：给 ② 加**一行成功打印** ✓（与归档同形：`W1b: private scratch repair attempt` ✓），
   使"是否执行、几次、成功否"在真机上**可见** ✓（⇒ 与第 58/74 轮补读数同法 ✓）。
```

## 三、下一步（✓）

```
① 读 ② 的 `if (repair_target != 0U)` 所在块与 `step` 分支的**包含关系** ✓；
② 补一行打印 ✓ ⇒ 真机确认 w1 是否有修复轮 ✓；
③ 若缺 ⇒ 按第 81 轮清单补 **`w1_scratch_repair_attempts` 循环 + `usleep(50000)` + 归档同形两串** ✓；
④ 判据不变：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
