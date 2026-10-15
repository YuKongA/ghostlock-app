# W2 探针：阻塞点归档（write-lands probe · 2026-10-12）

> 目的：**下次别再走这三条死路**。本文只记实测与阻塞，不含推测。
> 结论：**write-lands 探针在本树不可实现**（缺「以任意目标发一次窗口写」的入口）；
> 原语 `recv_bytes` 已**撤回**（`grep -rn recv_bytes core/src` ⇒ 0 ✓），树保持 `check` **109 PASS / 0 FAIL**。

## 1. 探针想回答什么

真机基线：`param-invalid = 0` ✓ · `w1 => won` ✓ · `value=9 = 1` ✓ · `uid=2000 ×16` ✓ ⇒
**唯一待答**：窗口写**是否真的落地**（⇒ 决定「写没落地」还是「Credential 页内容错」两条嫌疑）。

## 2. 两次能力核对（⇒ 均**不成立**，照实记录）

```
· 核对一（读内核字段）：**无「任意内核地址读」原语** ✓
  - 既有哨兵读的是**我们自己**的页镜像：main.cpp:759 `page = page_image.data();` ⇒
    main.cpp:1010 `before = …(page)` ⇒ `w+0x1180/+0x1198…` 是**页内偏移** ✗ ⇒ **看不到内核 `task+0x798`**
  - 全树仅两处 `read_word` 实现都读 **perf mmap 用户映射**
    （backend/victim/victim_process.cpp:51 ／ tools/perf_ops_probe.cpp:50）⇒ 非内核读 ✗
· 核对二（读回自己映射）：**内核写的是 skb 页，不是我们的用户页** ✗
  - main.cpp:994 `send_bytes(shaping_fd, page_image…, 65536)` ⇒ 数据进**对端接收队列（内核 skb）** ✓
  - main.cpp:995-999 紧接**两端 `close_fd`** ⇒ 队列连 skb 一起被丢弃 ✗
  - seab face 仅有 `send_bytes`（heap_ops.hpp:48 ／ heap_ops_linux.hpp:97）⇒ **无 `recv_bytes`** ✗
  ⇒ ⇒ **⇒ 若按「回读 page_image」做 ⇒ 会得**假阴性**✗（⇒ 会被误读成「写没落地」）**
```

## 3. `recv` 方案的三条硬性条件（⇒ 均已写死；仅供未来复用）

```
· 条件 A（时序）：send → **一次窗口写** → recv → 再 close（现有码是 send → close ✗ ⇒ 探针须把 close 推后）
· 条件 B（同页）：写目标 ＝ `prepared.base` 那张页（设备逐字 `payload page at 0xffff800000047180` ✓，
  与 `WordsRegion::base()+kPayloadOffset()` 同源 ✓，words_region.hpp:63 ✓）＋ 自选偏移 ⇒ marker 才在被 recv 的字节里
· 前置判据：**先核 `recv` 字节数 == 65536** ✓ ⇒ 只有成立时，marker 有/无才等价于「落地／没落地」；
  不成立 ⇒ **判「不确定」**，**不许硬判** ✓（⇒ 本设计正靠「页释放后复用」⇒ 页可能已易主 ✗）
```

## 4. 实现阻塞（⇒ 本次停手处）

```
本树**没有**「以任意目标发一次窗口写」的入口：
· main.cpp:1246 `run_window_stage_with_attempt(named_step, stage_request, stage_context)`
  ⇒ 两者按**该步自己的目标**建好 ⇒ 想写**我们的页** ⇒ 要再建**一整套 stage** ✗
· main.cpp:1265+ 修复轮（W2b：再 prepare + 建第二份页 + send + 跑一轮）⇒ 同样全套 ✗
  且会**扰动修复流程** ✗
⇒ ⇒ **⇒ 探针按批准原样不可实现**（需**新路径** ⇒ 按纪律先报 ✓）⇒ Lead 裁定收线（c）✓
```

## 5. 已排除项（⇒ 供后续直接引用，不必重查）

```
· 直映射守卫**从未触发**：`grep -c direct-map runH.out` ⇒ 0 ✓；`accept step=w2 … fam=1` ✓
  ⇒ 目标 0xffffff896a4bf398 / 0xffffff896dfdbd98 均在 [0xffffff8000000000, +64 GiB) 内 ✓
· `payload.delta = 0xfffffffffffff180`（负）**不参与 w2 目标**：只被 parity 预检消耗
  （main.cpp:452 `cand = base + 0x100`；:456 奇偶判据对 w2 **本就跳过**）✓
· 目标/值符合退役语义：目标 = `spawned.task + victim_cred_off`（main.cpp:685/822），
  `victim_cred_off` = wire `abi.task_struct.cred = 0x798` ✓；值 = `alias base + abi.offset.init_cred` ✓
  ⇒ 与 legacy/baseline-20261008:src/core/backend/cve_2026_43499/steps.cpp:165（`child_task + task_cred_off()`, mode 2）逐字一致 ✓
· 布局层与权威逐字一致：`pc = target−8` ✓／`left = target−8` ✓／`want_value = credential_value` ✓
  ⇒ memory/write_layout.cpp:15-23 ＋ :31 ✓
· `uid=0xffffffff`（重试日志）**无信息**：它是 main.cpp:121 的默认值，仅当通道报 0 时在 :1461 被覆盖 ✓
  ⇒ 有信息的是 main.cpp:1454 `main: victim uid=<u>`（每次回答都打）⇒ 实测 2000 ×16 ✓
```

## 6. 未验证项

```
· 窗口写**是否落地**（⇒ 无任意目标写入口 ⇒ 无法证）✗
· socket 回收路径是否把 skb 内容清掉/释放（⇒ 未读透）✗
· `victim_report_uid` 的 pipe 语义（「回答」vs「超时」判据）✗
· 「页是否已易主」⇒ **无法离线判** ⇒ 只能靠探针跑完的字节数核（⇒ 该核已在条件 3 里写死 ✓）
```
