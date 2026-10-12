# 43499 · 切片 2 的真正内容查明了：`fast_repair` 唯一的作用是"进入延迟 5000 µs" ✓✓

## 一、退役全景（✓，逐处 ✓）

```
route_policy.hpp:54   SelectPolicy : `static constexpr bool w2_fast_repair = **false**;`（空钩子 ✓）
route_policy.hpp:105  TcpPolicy    : `= **true**`；MulticastPolicy 有真实现 ✓
steps.cpp:89          在 `retry_write_stage` 的 attempt 循环里、`mode == Credential` 时：
                        `if (!M::w2_fast_repair_prebuild(session)) return false;`   ← **路由前** ✓
multicast_waiter_route.cpp:170/187  预建：用修复请求 `data_alias(init_cred)+8, Zero, leaf=1` ✓
                        调 `spray::prepare_good_kernel_page` + `stash_prebuilt_page` ✓；
                        激活：`activate_prebuilt_page()` ✓ + `fast_repair.store(1)` ✓ + 跑修复路由 ✓ + `store(0)` ✓。
race/threads.cpp:169  该标志**唯一**的消费者：
                        `race.reset(fast_repair ? **5000** : profile.select_enter_delay_us(), …)` ✓
```

## 二、推论（✓，切片 2 因此变小 ✓）

```
`fast_repair` 的**全部作用** = 让"激活那一次"的 `select` 进入延迟从 profile 值（真机 50000 µs ✓）
压到 **5000 µs** ✓ —— 目的：修复页**已经**就位（预建 ✓）⇒ 不需要那 50 ms 的等待 ✓。
⇒ ⇒ 因此切片 2 的**行为差异**只有两点：① 用**存好的页**（我已落 `page_slot.hpp` ✓ 待接线 ✓）；
   ② 该次路由的**进入延迟 = 5000 µs**（本树目前用 profile 值 ✗）。
```

## 三、落码清单（✓，下一步 ✓）

1. 读本树修复轮的构造点 ✓，把 `run_prepare` + 建页**提前到主路由之前** ✓，结果存进 `PageSlot` ✓；
2. 修复轮改用 `slot.activate(...)` 的字节 ✓（⇒ 不再重建 ✓）；
3. 修复轮那一次路由的进入延迟取 **5000 µs** ✓（⇒ 与退役同源 ✓；数值仍走 profile/wire ✗ 不写死 ✓，
   第 26 轮的定位 ✓：本树该值来自具名键 `backend.cve_2026_43499.route.select_stack.enter_delay_us` ✓
   （`profile/key_catalog.hpp:146` ✓；`expected_keys.hpp:53` 亦登记 ✓）⇒ 真机夹具该键 = **50000** ✗；
   退役在 `fast_repair` 生效时改用 **5000** ✓ ⇒ 修法：**仅在修复轮那一次**用 5000 ✓，主轮仍用 profile 值 ✓。
   或按退役写法由 `fast_repair` 派生 ✓）；
4. 出口全覆盖 `discard` ✓（守则 §5 ✓）；判据：`main: victim uid=0` ✓。

> **第 27 轮补注（归属 ✓）**：该 5000 µs 是交给 **`race.reset(delay, main_cpu, consumer_cpu)`** 的 ✓
> （退役 `race/threads.cpp:171-174` 逐字 ✓）⇒ 它属于 **race 层**，而本树 race 里**没有** `enter_delay` ✗
> （本树用的是**路由级** `consumer_delay_us` ✓，见 `multicast_route.hpp:243` ✓）⇒
> ⇒ 因此第 3 项**不能**单独小改 ✗ ⇒ 它与第 1/2 项**同属** `fast_repair` 那套机制 ✓（同批做 ✓）。

---

> **第 29 轮补注（行锚点 ✓，切片 2 据此即为机械操作 ✓）**：修复块在 `core/src/main.cpp` 的 `:1143-1207` ✓。
> 提前点 = `:1145 run_prepare(prepare, prepared2, &repair_refusal)` ✓（连同 `:1143` 与 `:1151/:1156-1165` 的建页 ✓）
> ⇒ 整体搬到**主路由（`:1112`）之前** ✓，结果存进 `PageSlot` ✓。
> activate 点 = `:1167-1169 send_bytes(prepared2.shaping_fd, repair_image.data(), …)` ✓ ⇒ 改为取 `slot.activate(...)` 的字节 ✓。
> 延迟点 = `:1207 run_window_stage_with_attempt(...)` ✓ ⇒ 该次进入延迟取 **5000 µs** ✓。
> 出口 = `:1175-1177` 的两个 `close_fd` 处一并 `slot.discard()` ✓（守则 §5 ✓）。

---

> **第 30 轮进度（✓，第一步接线已完成并在真机确认 ✓）**：修复页已改经 `PageSlot` 流转 ✓
> —— 声明 `cve49::memory::PageSlot repair_slot{};` ✓、建页后 `stash` ✓、发送前 `activate` ✓；
> stash 与 activate 之间只有 `memcpy` ✓ ⇒ 无早退窗口 ✓。补 `#include …/memory/page_slot.hpp` ✓。
> 真机：`repair-slot-refused` 计数 **0** ✓；`route status=0/0 attempts=16 calls=1 successes=1` ✓（无回归 ✓）；
> `repair target=0xffffff802abfd590 mode=1 preserve=1` ✓；`victim uid=2000` ✗（仍未 root ✓）。
> 门禁：`check` **109 PASS** ✓ / `android` 0 警 ✓。
> 待做：① 把 prepare+建页**提前到主路由之前** ✓（`:1145`/`:1151`/`:1156-1165` ⇒ 搬到 `:1112` 前 ✓）；
> ② 修复轮进入延迟取 **5000 µs** ✓（`race` 层入口待补 ✗）。

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
