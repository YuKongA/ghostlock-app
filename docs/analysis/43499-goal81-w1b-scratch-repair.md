# 43499 · 第 81 轮：**W1b「私有 scratch 修复」全貌**（含 quarantine/release）✓✓✓

## 一、退役实现（✓✓，`steps.cpp:285-321` 逐字 ✓）

```cpp
/* W1b: the non-resident multicast middleware repairs its **private scratch** … */
bool w1_scratch_repair(CoreSession &session) {
    const uintptr_t w1_scratch_poison = heap.current.base + mcast.buffer_size;   // 我树已知该式 ✓
    … "W1 scratch page **quarantine** failed" ✗                                  // ★ 隔离 ✓
    for (repair_try = 1..w1_scratch_repair_attempts) {                          // ★ 配置键（夹具已有 ✓）
        pr_info("W1b: private scratch repair attempt %u/%u\n", repair_try, attempts);
        scratch_repair = WriteRequest::make(w1_scratch_poison, **WriteMode::Zero, 1**);   // Zero + leaf=1 ✓
        if (attack_write<M>(session, scratch_repair, "W1b: private scratch repair")) { repaired = 1; break; }
        usleep(50000);
    }
    if (repaired) { "private scratch repaired; **releasing quarantine**" ✓;
                    release_quarantined_reclaim_sockets(); return true; }
    "private scratch repair failed; **keeping page quarantined**" ✗
}
```

## 二、含义（✓✓✓）

```
① W1 之后，那一页被**隔离（quarantine）** ✓；② 只有"私有 scratch 修复"成功后才**释放** ✓；
③ 修复请求 = **Zero + leaf=1** 写 `base + mcast.buffer_size` ✓（⇒ 我树第 32 轮已认过这个目标 ✓）；
④ 本树：**无 quarantine、无该修复、无该读数** ✗（第 80 轮的有无对照已证 ✓）
   —— 而**配置键 `w1_scratch_repair_attempts` 已存在** ✓（夹具/目录皆有 ✓）⇒ ⇒ **键在、行为未落** ✗。
⇒ ⇒ **⇒ 若 W1 的写会污染 scratch（页内的临时区）✓ ⇒ 不修复 ⇒ 后续 W2 用的页/状态不干净** ✓ ⇒
   ⇒ ⇒ **⇒ 这很可能正是"W2 写不生效"的直接原因** ✓✓✓（与本会话全部读数吻合 ✓）。
```

## 三、下一步（✓，可落码 ✓）

```
① 读本树既有实现：第 32 轮我已定位过"修复轮"（`main.cpp:1143-1207` ✓）与 `w1` 分支的 `repair_target = prepared.base + buffer_size` ✓
   ⇒ ⇒ **目标一致** ✓ ⇒ 只差：**时机**（W1 之后、W2 之前 ✓）与**轮数键**（`w1_scratch_repair_attempts` ✓）；
② 按退役同形补：`for (try = 1..键) { Zero+leaf=1 写 base+buffer_size ⇒ 成功即 break; usleep(50000); }` ✓
   + 打印 `W1b: private scratch repair attempt`/`private scratch repaired` ✓（⇒ 与归档同形 ✓）；
③ 判据：真机出现 `private scratch repaired` ✓ ⇒ ⇒ **再看 `main: victim uid=0`** ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
