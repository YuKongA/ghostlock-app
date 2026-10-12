# 43499 · 第 47 轮：waiter 的**字段偏移**从哪来（夹具里为空 ✗）✓✓✓

## 一、夹具现状（✓）

```json
route.multicast_waiter = {
  "abi": { "waiter_off": "0x60", "task_offset": "0x30", "lock_offset": "0x38", "buffer_size": "0x108" },
  "execution": { "arm_hold": 20000, "arm_sequence": 16, "attempts": 128 }
  // ⇒ 没有 "waiter" 子键 ✗ ⇒ pc / right / left / task / lock / wake_state 的字段偏移**不在文档里** ✓
}
```

## 二、推论（✓✓）

```
本树建页要用 `inputs.waiter.pc/.right/.left/.task/.lock/…`（`payload_page.hpp` ✓）⇒ 若这些**未被加载**，
它们就是默认 **0** ✗ ⇒ 于是**全部字段都写到 `waiter + 0`** ✗ ⇒ 相互覆盖 ⇒ 最终留下的是"最后一个写入者"✓；
真机读数与之吻合 ✓：
  · `w+0x00` = **1** ✓（`head_word = 1U` 是最后写的之一 ✓）；
  · `w+0x18`（pc ✗）/ `w+0x28`（left ✗）**不是**期望值 ✗（⇒ 那里根本没被按预期写过 ✓）。
⇒ ⇒ 因此第 46 轮"pc/left 没写对"的根因**很可能就是：字段偏移没有数据来源** ✗✓。
```

## 三、下一步（✓，一处读 + 一处判 ✓）

```
① 读 `payload_page.hpp` 的 `WaiterFieldOffsets` 与**谁填它**（`inputs.waiter = …` 的赋值点 ✓）；
② 判定：该赋值是否来自文档键 ✗（⇒ 若来源键**不存在**（本夹具 ✓）⇒ 是否**具名拒绝** ✓ 还是静默 0 ✗ ——
   按本树纪律应是前者 ✓；若现状是后者 ⇒ 那就是**静默 0** ✗ ⇒ 修：缺键即具名拒绝 ✓，
   并把缺失的键补进夹具（⇒ 值取内核 5.15 的 task_struct/rt_mutex_waiter 布局 ✓，**不得凭空造数** ✗，
   须有出处：`abi.task_struct.*` 已在夹具里 ✓ ⇒ 从同一处引 ✓）；
③ 判据：真机 `w+0x1198` = `value` ✓、`w+0x11a8` = `target` ✓（全 64 位 ✓）⇒ 再看 `uid` ✓。
```

## 四、门禁（✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
