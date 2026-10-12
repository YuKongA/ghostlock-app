# 43499 · 收束：page 的 base 只差"一档 0x8000"（round 211）✓

## 本轮查清的三件事（✓）

```
1. 夹具自带 Lua（`step.code` = base64 的 procedures/43499_window.lua ✓），它：
     procedure.steps = { "w1", "w2", "w3" } ✓；并 assert 文档的四个 stages 键 ✓；
     两夹具的 `execution` 段都有 `stages` ✓（递归 diff 为空 ✓）⇒ **断言通过** ✓；
2. 日志里 `main: step result w1 => payload-layout-not-accepted` ✓ ⇒ **w1 真的跑了** ✓；
3. 所以失败点唯一：**payload 门** ✗（accepts=0 ✓）。
```

## base 的规律（本轮 ✓，样本对照）

| 组 | base 样本 | bit16（= (base+0x100)>>16 & 1） |
| --- | --- | --- |
| canonical（9/9 ✓） | 0x010000 / 0x018000 / 0x030000 / 0x038000 / 0x0d8000 ✓ | **全 1** ✓（1/1/3/3/13 ✓） |
| select（8/8 ✗） | 0x8000 ✗ | **0** ✗ |

⇒ ⇒ 两者差**一档 0x8000**：select 的页落在**低半档** ✗ ⇒ 奇偶自检必然 ok=0 ✓
⇒ ⇒ 而奇偶自检的 8 次重试**不动 base** ✗（同址 ✓）⇒ 无法自愈 ✓。

## 下一步（下一批 ✓）

```
查"页基址落在哪一档"由什么决定 ✓：
  · `prepare` 的 spray 参数（两夹具相同 ✓）⇒ 不是它 ✗；
  · 更可能是**运行时的堆/探针结果**（`snitch` 找到的地址 ✓ / `prepared.base` 的来源 ✓）；
  · 对照 canonical 为何**总是**落在 ≥0x10000 的档 ✓ ⇒ 找到那个"下限/取整"逻辑 ✓，
    再看 select 路径是否少了同一步 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
