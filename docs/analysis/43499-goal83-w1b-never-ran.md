# 43499 · 第 83 轮：**w1 的修复轮在真机上从未执行**（大概率被静默跳过）✓✓✓

## 一、证据（✓✓✓，三份真机日志 ✓）

```
`grep -c "main: repair "` ⇒ /tmp/gd_dev.txt 30 ✓ / /tmp/cl3.txt 23 ✓ / /tmp/base44.txt 25 ✓（共 78 ✓）；
而**全部 78 次**的 target 都是同一个值：
  `main: repair target=**0xffffff802abfd590** mode=1 preserve=0` ✓ ← 只有 **W2 的修复**（`init_cred + 8` ✓）；
`repair-prepare-refused` 计数 **0** ✓（⇒ 不是 prepare 失败 ✗）；
⇒ ⇒ **⇒ W1 的修复（目标应为 `prepared.base + buffer_size` ✓）一次都没出现** ✗✓。
```

## 二、推断（✓✓）

```
本树 w1 分支**确实**写了 `repair_target = prepared.base + route_values.buffer_size` ✓（第 32 轮已读 ✓）；
⇒ 而它从未执行 ⇒ 最可能是 **`repair_target == 0`** ✗ ⇒ 被 `if (repair_target != 0U)` **静默跳过** ✗✓；
⇒ 拆分两种可能：① `route_values.buffer_size == 0` ✗（⇒ 键未读到 ✓）；② `prepared.base == 0` ✗；
⇒ 对照退役：同一量取自 `layout.buffer_size` ✓ = **0x108** ✓（**夹具 abi 里就有** ✓：`"buffer_size": "0x108"` ✓）；
⇒ ⇒ **⇒ 若本树从另一个键读（或没读）⇒ 就是"W1b 被静默跳过"** ✓ ⇒ 而这正是 W1 污染无人修复的后果 ✓✓。
```

## 三、下一步（✓，一处读 + 一处打印 ✓）

```
① 读本树 `route_values.buffer_size` 的**填充键** ✓（⇒ 应在 `route_config.cpp` 的读键处 ✓）；
② 打印它（一行 ✓）⇒ 真机确认是否为 0 ✗；
③ 若为 0 ⇒ 改用夹具已有键 `…route.multicast_waiter.abi.buffer_size = 0x108` ✓（不造数 ✓）；
④ 判据：真机出现 `main: repair target=…`（**非 init_cred 的那个** ✓）⇒ ⇒ **再看 `victim uid`** ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
