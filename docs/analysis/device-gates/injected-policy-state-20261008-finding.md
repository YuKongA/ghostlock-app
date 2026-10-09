# 真机发现：注入的策略在调用内被写入，调用方读回的是初始值（2026-10-08）

> 级别：**攻击关键路径相关**（页准备↔搜索的接合依赖它）⇒ 按 AGENTS 归档
> 设备 `QV770MFGJ1` · 探针 `tools/snitch_prepare_probe.cpp`

## 1 现象（原始输出）

```
[id] probe &policy=0x7ff046f9f0
[id] prepare call site &snitch=0x7ff046f9f0     <- 身份相同 ✓
[id] start this=0x7ff046f9f0 ran=0
[id] after search status=plan-missing ran=0 this=0x7ff046f9f0
[id] set ran=1 this=0x7ff046f9f0                <- 调用内确实写了 1 ✓
  policy:  ran=0 has=0 addr=0 outcome=geometry-missing rounds=0 probes=0 wanted=0 region=0x0
  prepare: status=plan-missing at=7 opened=5 closed=3   <- 准备自己的结果却正常 ✓
```

## 2 已经排除的

- **不是拷贝**：`&policy` 与 `&snitch` 与 `this` **同一地址** ✓；
- **不是旧二进制**：`-B` 强制重建后现象不变 ✓（AGENTS 警告过的 mtime 陷阱已排 ✓）；
- **不是没调用**：同一 `start` 里前后两行都打出来了 ✓；
- **不是 printf 的问题**：探针用的**访问器**（`has_collision()`）也读到 0 ✓；
- **探针自己先调一次则完全正常** ✓（`started=1 ran=1 rounds=10` ✓）。

## 3 结论与下一步

**写入发生在调用内 ✓、身份同一 ✓、而调用方读回初始值 ✗** ⇒ 这是**内存/ABI 层面的分歧**（最可能是两侧对 `SnitchPolicy` 的**布局**理解不同 ✗，或优化器对别名做了假设 ✗）。

**下一轮的决定性测量**（不再猜 ✓）：**在 `start` 内与探针内各打印 `sizeof(*this)` / `sizeof(policy)` 与 `offsetof` 关键字段** ⇒ 若两侧不一致 ⇒ 布局/ODR 问题 ✓；若一致 ⇒ 优化器/别名问题 ✓。

**顺带的影响** ⚠：在解释清楚之前，**该探针自身的报告不可信** ✗（它读的是陈旧的策略状态 ✓）⇒ 不得据此判定接合成功/失败 ✓。

## 4 追加测量（同一轮）：**布局已排除** ✓

```
[lay] probe sizeof=288 off(ran)=280 off(outcome)=216 off(buckets)=208 &policy=0x7fec562ea0
[lay] sizeof=288 off(ran)=280 off(outcome)=216 off(buckets)=208 this=0x7fec562ea0
[lay] sizeof=288 off(ran)=280 off(outcome)=216 off(buckets)=208 this=0x7fec562ea0   <- 准备的调用
  policy:  ran=0 has=0 addr=0 outcome=geometry-missing rounds=0 region=0x0
```

⇒ **两侧的 `sizeof` 与三个偏移逐一相同 ✓、地址相同 ✓** ⇒ 排除布局/ODR 分歧。

## 5 唯一剩下的解释（下一轮测它）

「调用内写了 `ran=1`」（上节输出）与「调用方读回 0」**不可能同时发生在一个进程里** ⇒
**那两行来自不同进程** ⇒ 即 **某个 fork 出的孩子没有退出，把调用方余下的代码跑了一遍**（它拿到的是 `ran=0` 的副本）⇒
**决定性测量**：在探针每行、以及每个孩子分支入口各打印 **`getpid()`** ⇒ 立刻看出是谁在报告。

**注意**：这与第 3 节「探针报告不可信」是同一条 ⇒ 在定位前，**不得据该探针判定接合成败**。

## 6 追加测量（同轮）：四种经典解释全部排除

```
# 进程：全程同一个 pid（含报告行）
[pid] start pid=20978 this=0x7fe87e7730
  self-call: started=1 ran=1 status=plan-missing rounds=19
[pid] prepare child pid=20981 … 20982 exiting
[pid] start pid=20978 this=0x7fe87e7730        <- 准备的调用，同一个 pid
[pid] prepare child pid=20983 … 20986 exiting
[pid] report pid=20978                          <- 报告也是同一个 pid
  policy:  ran=0 …                               <- 仍读回初始值

# 优化等级：-O0 与 -O2 现象完全相同
  policy:  ran=0 has=0 addr=0 outcome=geometry-missing rounds=0 region=0x0（-O0 亦同）
```

| 假设 | 判定 | 依据 |
|---|---|---|
| 拷贝 | **排除** ✓ | 三处地址相同 ✓ |
| 布局/ODR | **排除** ✓ | `sizeof=288`、三个偏移逐一相同 ✓ |
| 走散的子进程 | **排除** ✓ | 报告行与调用同一 pid ✓ |
| 优化器/别名 | **排除** ✓ | `-O0` 现象不变 ✓ |
| 变量遮蔽 | **排除** ✓ | 探针内只有一处 `policy` 声明 ✓ |

## 7 下一步（唯一的仪器）

**让 `prepare_page` 往一个调用方提供的内存格里写一个字节**（直接可见性证明 ✓）：

- 若那个字节在调用方**可见** ⇒ 说明普通写入能跨该调用边界看见 ⇒ 问题在 `SnitchPolicy` 这个对象本身（例如某处重建/析构 ✗）；
- 若**不可见** ⇒ 说明该调用边界在真机上有更基础的问题（栈/寄存器 ✓），须换更原始的手段看。

**在此之前，该探针的报告不得用作判据** ✓（见 §3 ⚠）。

## 8 决胜测量：**那块内存根本没被写过**

```
  witness: flag=1 value=42          <- 同形状的普通跨调用写读 **可见** ✓（边界没问题）
  raw tail: [280]=0 [216]=3 [217]=0 <- 调用**前**：ran=0，outcome=3（初始值）
  self-call: started=1 ran=1 …      <- 直接调用策略：**正常** ✓
  policy:  ran=0 …
  raw tail after prepare: [280]=0 [216]=3 [217]=0   <- 调用**后**：与调用前**逐字节相同** ✗
```

⇒ **绕开成员读、按字节读**（`-O0` 下同样）⇒ **该对象的内存自始至终没变** ⇒ 也就是说：
**`start` 的那些打印（同地址、写 ran=1）与这块内存不属于同一个对象实例**。

## 9 下一仪器（只剩这一处矛盾）

在 `start` **内部**同时打印：**`getpid()` + `this` + 该内存的字节 `[280]`**⇒ 三个值放在一行 ⇒ 立刻看出：

- 若 `start` 内 `[280]` 也是 0 且写完变 1 ⇒ 那么调用方那块内存确实没被写 ⇒ **两个地址空间**（fork ✓）；
- 若 `start` 内 `[280]` 一直是 0 ⇒ 写入落在别处 ⇒ 追 `this` 的来源 ✓。

## 10 决定性对照：**返回值路径正常，状态路径不正常**

```
  self-call: started=1 ran=1 status=plan-missing rounds=9   <- 直接调用：一并正常 ✓
  prepare: status=plan-missing at=7 opened=5 closed=3 base=0  <- 准备自报：无碰撞（状态路径）
  search (by value): started=1                              <- **返回值**路径：正常 ✓
```

⇒ 同一轮里，**同一个策略**：
- **按返回值看**（`start` 的 bool ✓）⇒ **正常** ✓；
- **按对象状态看**（`has_collision()` / `ran` / `outcome` ✓）⇒ **始终读到调用前的值** ✗；
- **按字节看**（绕开成员读 ✓）⇒ **内存确实没被写过** ✗。

## 11 处置（工程决定，绕开谜题而不再追它）

**生产路径一律走返回值/出参，不依赖策略对象的状态回读** ✓ ⇒ 因为：
① **返回值路径在真机上已验证正常** ✓（本节第一条 ✓）；
② 页准备**自己的 outcome** 就是它该报告的东西 ✓（`base` / `opened` / `closed` / `status` ✓）—— 它**不需要**再去读策略 ✓；
③ 谜题本身**已归档**（§1–§9 ✓：拷贝/布局/进程/优化/遮蔽/边界**逐一排除** ✓）⇒ 它**不再阻塞**接合 ✓，只在有人要依赖策略状态回读时才会重现 ✓。
