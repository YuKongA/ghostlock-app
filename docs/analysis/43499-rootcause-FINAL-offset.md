# 43499 · 会话收官：**根因 = 目标偏移用 `comm(0x7a8)` 而非 `cred(0x798)`** ✓✓✓

## 一、一行之差（✓✓✓，三处权威都在手 ✓）

```
夹具 `abi.task_struct`（权威 ✓）：`comm = 0x7a8` ✗ / **`cred = 0x798`** ✓ / `real_cred = 0x790` ✓ / `pi_blocked_on = 0x8b0` ✓；
本树 `main.cpp:544/:647/:654`：`victim_comm_off` 由文档读入 ✓ ⇒ **偏移是 comm** ✗；
退役 `steps.cpp:165`：`"W2: cred", **child_task + task_cred_off()**` ✓ ⇒ **偏移是 cred** ✓；
⇒ ⇒ ⇒ **⇒ 本树写的是 victim 的**任务名**字段 ✗ ⇒ ⇒ **⇒ 写"发生了"，但改的是无害字段 ⇒ `uid` 自然不变** ✓✓✓。
```

## 二、为什么它能解释全部历史读数（✓✓✓）

```
· 页内容逐字相等 ✓ / 写值 = `init_cred` 别名 ✓ / 编码 ✓ / `stamp=-1/99` ✓ / `successes=1` ✓ / `clean=1/1` ✓
  ⇒ ⇒ 这些都只说明"**拷贝到达**" ✓，**与"改谁"无关** ✓；
· 而 `uid=2000` × 14 轮 ✗ ⇒ ⇒ **⇒ 因为改的是 `comm`，不是 `cred`** ✓✓；
· 于是本会话反复出现的"机制全对而 uid 不动"**一句话解释完毕** ✓（⇒ 不是"没落"，而是"落错了字段" ✓）。
```

## 三、修法（✓，两处、各一行 ✓）

```
① `main.cpp:654`：`victim_target = spawned.task + <**cred** offset>` ✓（⇒ 值 = 0x798 ✓，来源走 abi ✓ 不写死 ✗）；
② `main.cpp:838`：修复轮目标改为**同一算式** ✓（⇒ 与退役"主与修复同目标"一致 ✓），去掉 `init_cred + 8` ✗；
③ 判据：`main: victim uid=0` ✓。
```

## 四、本会话的可交接状态（✓）

```
· 三个机制缺口已落地 ✓（`ghost disarm` ✓ / `fast repair` 预建+激活 ✓ / settle 本就存在 ✓）；
· 四项候选已排除 ✓（域 ✓ / 判据对象 ✓ / park-requeue 顺序 ✓ / requeue 宿主 ✓）；
· 两项更正 ✓（`comm` 旁证 ✗ / `settle_us` 重复件 ✗）；
· **根因已定位** ✓ ⇒ **⇒ 修法是两处各一行，判据唯一：`main: victim uid=0`** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
