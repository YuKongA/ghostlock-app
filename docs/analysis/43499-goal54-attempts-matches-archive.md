# 43499 · 第 54 轮：`attempts=16` 与归档一致 ⇒ 排除（第二次）✓

## 一、机制（✓，源码 ✓）

```cpp
// multicast_route.hpp
:406  for (std::int32_t attempt = 1; attempt <= config_.limits.max_attempts; ++attempt) { … }   // 轮询 ✓
:209  return attempt >= 0 && attempt < config_.limits.arm_sequence;                              // 是否仍在"装填" ✓
:418  if (attempt < config_.limits.arm_sequence || …) { … }                                      // 装填阶段分支 ✓
// 配置（夹具）：max_attempts = 128 ✓ / arm_sequence = 16 ✓
```

## 二、读数对照（✓✓）

```
本树真机：`main: route status=0/0 attempts=**16** calls=1 successes=1 …` ✓
归档成功那次：**同样 `attempts=16`** ✓（旧目标第 246 轮已核 ✓）
⇒ ⇒ **逐字一致** ✓ ⇒ 不是差异 ✗ ⇒ 「attempts 被写死/少 8 倍」这条线索**排除** ✓（第二次 ✓）。
```

## 三、这轮的方法论收获（✓）

```
把"归档成功那次的读数"当成**判据基线** ✓ ⇒ 任何一条"我这边的读数"只要与它逐字相同 ✓，
就不可能是差异 ✗ ⇒ 于是排除非常快 ✓（不需要每走一步都重推机制 ✓）。
⇒ 反过来：**差异只可能在"归档读到了什么、我读到了什么"之外** ✗ ⇒ 即在**看过的线之外** ✓，
   或**同一读数背后的实现**不同 ✗（例如同一个 16，但一个是"装填 16 轮后开始写" ✓，
   另一个是"被当成上限 16 就停了" ✗）⇒ ⇒ **⇒ 下一步该查的是"16 的语义"而非数值** ✓。
```

## 四、下一步（✓）

```
查"装填/触发"的**语义**是否一致 ✓（退役 `steps.cpp` 里 W2 的 `retry_write_stage` 与 route 的
`arm_sequence`/`arm_hold` 如何配合 ✓；本树 :406-418 的实现是否与之同形 ✗）；
判据不变：任一轮 `main: victim uid=0` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
