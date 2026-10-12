# 43499 · 更正：`Running` 就是真判据（round 181）✓

## 更正（第三处自纠 ✓）

```
我上一轮（180）把这处判据称作"**代理**判据"✗ ⇒ 读 PiRace::run 的注释后更正 ✓：

  原注释（pi_race.hpp:120-122 ✓）：
    "The count of waiters the kernel moved is the evidence: a count of one is the kernel
     saying a waiter was really there, and zero means there was none to move."
    if (Seam::cmp_requeue_pi(chain_, target_) < 1) { outcome_ = RaceOutcome::Refused; … }

⇒ 即：**Running ⟺ 内核真的搬动了一个等待者** ✓ ⇒ 与快照的 `consumer_success > 0` 同一件事 ✓
⇒ 我的判据**本来就应该**是它 ✓ ⇒ 代码已收紧为 `raced == RaceOutcome::Running` ✓（不再用"非被拒"这种宽判 ✗）。
```

## 为什么这很重要（对真机判据 ✓）

```
`Running` 只有在 **cmp_requeue_pi 返回 >= 1** 时才给 ✓ ⇒ 真机上它能直接回答：
  "内核到底有没有把我的等待者搬过去" ✓ —— 这正是此前的 `waiter=-1` ✗ 想回答的问题 ✓；
⇒ 接选择器之后，第一件事就是看这个：
   Running ✓ ⇒ 机制在这一条 route 上**通了** ⇒ 再看 `enforce` 1→0 ⇒ 再看 `uid=0`；
   Refused ✗ ⇒ 内核没搬 ⇒ 说明该 route 的形状/词表/时序还有差（此时才谈下一步 ✗）。
```

## 判据（本轮 ✓）

```
android ⇒ RC=0，0 error / 0 warning ✓；check → RC=0，108 PASS ✓；lint → 0 ✓
```

## 三处自纠一览（今晚 ✓，全部自己抓到 ✓）

| # | 误判 | 更正 |
| --- | --- | --- |
| 1 | "页面元组错位" ✗ | 是我**用错了分支**（preserve 支 ✓）⇒ 字节其实对 ✓ |
| 2 | "stamp 该在哪张栈" ✗ | 那是**内核**的拷贝 ✓，与我们缓冲位置无关 ✓ |
| 3 | "这是代理判据" ✗ | `Running` **就是**真判据 ✓（本轮 ✓） |
