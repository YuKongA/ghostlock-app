# 43499 · 修复轮并入窗口路径：完成 + 判据达成（round 140）

## 做的三件事

1. **上提共享值**（搬移的前置 ✓）：`kOrder3`/`kChunks`/`kAliasBase`/`alias_image_delta`/`page_inputs`/
   `layout_inputs` ⇒ 从"步骤建页块"上提到函数作用域 ✓；
2. **搬移修复轮**：806–884 那一段 ⇒ 主轮报告之后（在 `window_guard`/`named_step` 的作用域内 ✓）；
3. **改调用路径**：`(void) repair_route.execute(…)` ⇒ 照抄主轮的窗口四件套（同一个守卫 ✓ +
   `WindowStageContext`/`WindowStageRequest`/`run_window_stage_with_attempt` ✓）。

## 判据（达成 ✓）

```
（第 1 次跑，RC=0）
main: route status=0/0 attempts=15 calls=1 successes=1        ← 主轮 ✓
main: repair route status=0/0 attempts=15 calls=1 successes=1  ← ★ 两轮修复都 `0/0` ✓（此前 2/61 ✗）
main: step=w1/w2/w3 … accepts=1 ✓
main: victim uid=2000 ✗
```

## 过程中自己引入又修掉的一个 bug（如实 ✓）

```
上提 alias_image_delta 后 ⇒ 它在 phys_load/phys_offset **赋值之前**就被算出 ⇒ 结果为 0 ✗
  ⇒ 真机显形：main: step=w1 target=0xffffff8002daad88（少了 +0x28000000 ✗）
修法：在上提处**就地读一次** δ（kernel_phys_load − kernel_phys_offset ✓）
  ⇒ 真机复原：main: step=w1 target=0xffffff802adaad88 ✓（= 归档值 ✓）
```

## 已知的偶发（不是新问题 ✓）

- 奇偶仍偶发不中 ⇒ `payload-layout-not-accepted`（如 140 轮最后一次 ✓）；
- round 136 量到 3/3 全中 ✓；范围约 3/4 ~ 1 ✓ ⇒ 属**分布**，不是确定性 ✗（也不影响"两轮修复 0/0"这条判据 ✓）。

## 门禁

- check → RC=0，108 PASS ✓；android → RC=0（0 警）✓；lint → 0 ✓。
