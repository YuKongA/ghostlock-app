# 43499 · 第 52 轮：**页内容逐字正确** ⇒ 问题唯一落在内核侧 ✓✓✓

## 一、新仪器（✓，增量、安全 ✓）

```
在**建页之后**取样（旧哨斌在建页**之前**取 ✗ 且 after2 打的是 before 的拷贝 ✗ ⇒ 无用 ✓）：
  `pc_now`   = *(page + slots.waiter + waiter.pc)，`left_now` = *(… + waiter.left) ✓；
  并同时打印期望值 `write_layout.value` 与 `page_request.target` ✓ ⇒ 一次跑即可逐字对照 ✓。
```

## 二、读数（✓✓✓，逐字相等 ✓）

```
main: page-after-build step=w1 pc=0xffff800000010100 left=0xffffff802adaad88 want_value=0xffff800000010100 want_target=0xffffff802adaad88 ✓
main: page-after-build step=w2 pc=0xffffff802abfd588 left=0xffffff8937316198 want_value=0xffffff802abfd588 want_target=0xffffff8937316198 ✓
main: page-after-build step=w2 pc=0xffffff802abfd588 left=0xffffff896e0cab98 want_value=0xffffff802abfd588 want_target=0xffffff896e0cab98 ✓
⇒ ⇒ **页里的 pc / left 与期望逐字相等** ✓ ⇒ 编码链 + 页构建**双双正确** ✓。
```

## 三、被排除的层（✓✓，全部用户态可核项 ✓）

| 层 | 状态 |
| --- | --- |
| 写值（`value` = init_cred 别名 ✓） | **正确** ✓ |
| 目标（同窗、`accepts=1` ✓） | **正确** ✓ |
| 臂（第 34 轮回退后 `preserve` 正确 ✓） | **正确** ✓ |
| 编码（`arm_for` → `put_word` → 偏移 ✓） | **正确** ✓ |
| 页内容（本轮 pc/left 逐字相等 ✓） | **正确** ✓ |
| 路由（`successes=1` ✓） | **正确** ✓ |

```
⇒ ⇒ **⇒ 唯一剩下的未知：内核是否真的把值写到 `target`** ✓（与第 51 轮的收敛一致 ✓）。
```

## 四、下一步（✓，唯一的路 ✓）

```
**独立读回**（L 级 ⇒ 按守则 §2 先计划 ✓）：用同一条**别名**路径读回 `target` 处的字（或读受害者的
`cred` 字段 ✓），一次跑即可把问题二分：
  · **读回为 0 或旧值** ⇒ 内核根本没写 ✗ ⇒ 回到"擦除式写入是否真的发生"（⇒ 需要新的触发视角 ✓）；
  · **读回为新值** ⇒ 内核写了 ✓ ⇒ 问题在"受害者为何不据新 cred 报 uid" ✓（⇒ 查 cred 引用/生命周期 ✓）。
判据：`main: victim uid=0` ✓。
```

## 五、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```
