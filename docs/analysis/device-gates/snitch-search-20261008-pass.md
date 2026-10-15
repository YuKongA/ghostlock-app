# 真机门禁：43499 泄漏搜索端到端 + landing（2026-10-08）

> 级别：**攻击关键路径改动** ⇒ 按 AGENTS 需真机门禁 + 本记录 ✓
> 设备 `QV770MFGJ1`（A301SO · Android 15 · `5.15.189-android13-8-00016-g51bba4309aac-ab14546557`）
> 代码：`core/src/backend/cve_2026_43499/leak/{snitch_ops,snitch_ops_linux,snitch_scan,snitch_child,snitch_run,leak_search,leak_walk,snitch_region,snitch_setup,snitch_tuning,snitch_policy}`
> 提交：`3f1453b3`（搜索端到端）· `014d9250`（按名唤醒）· `56ed2f9c`（唤醒协议）· `8faf7a45`（搜索↔页准备接合）

## 1 三支真机探针（每支**有界**运行 ✓ —— 上一轮教训已落到工具形态 ✓）

### 1.1 landing（`snitch_ops_probe`）· 判据 PASS / EXIT=0 ✓

```
snitch landing probe
  map_shared          address=0x7fba35a000 errno=0 ok
  wait(mismatch)      value=-1 errno=11 ok        <- EAGAIN：证明 futex 调用真到了内核
  start_thread        handle=537555197104 errno=0 ok
  wake(1)             value=0 ok                  <- 未唤到人（等待者尚未入睡）而**无害**
  join_thread         value=0 ok
  the waiter came back  ok
  unmap               value=0 ok
snitch landing probe: PASS          EXIT=0
```

**这一行的价值** ✓：`wake(1) value=0`（**没唤到任何人**）而等待者**仍然返回** ✓ —— 正是「先查状态再睡、先改状态再唤」的健壮协议在起作用 ✓。**反例（同一探针首版）** ✗：用空转当延时 ⇒ 唤醒先到 ⇒ `join` 永久阻塞 ⇒ `timeout 10` ⇒ **EXIT=124 且零输出**（块缓冲）✓ ⇒ 该两条要求已写入 `falsification-hygiene.md` §4 ✓。

### 1.2 fork 隔离诊断（`snitch_fork_probe`）· PASS / EXIT=0 ✓

```
  parent: forked pid=8720
  child: running, pid=8720
  child: poke 0..4, counter=1..5
  parent: waitpid=8720 status=0, counter=5 word=5
snitch fork probe: PASS
```

⇒ 证明：**子进程会跑 ✓ 写落在共享映射 ✓ 父进程读得到 ✓**（当时我怀疑「poke 到不了父进程」✗ ⇒ **被这支探针证伪** ✓）。

### 1.3 整条搜索（`snitch_run_probe`）· PASS / EXIT=0 ✓

```
snitch run probe
  status=ok rounds=12 pokes_wanted=2048 probes=279 found=ffff800000022c00
  region readback: counter=2153 stop=1 bytes=8296
  unmap region: value=0 · unmap words: value=0
snitch run probe: PASS          EXIT=0
```

**修复前（同一探针，沿革保留 ✗）**：`status=plan-missing rounds=8 pokes_wanted=2048 probes=0` ✓ ⇒ 我据此**误判**为「poke 不可见」✗；其后加入的区域反读给出 `counter=114` ✓ ⇒ 证明**计数一直在涨** ✓ ⇒ 真因 = **父进程耐心太短** ✓ ⇒ 接入「每轮在计数上带时限地等」后 12 轮达标 ✓。

## 2 判据解读（写进记录以免误读 ✓）

- `probes=279` = 走查真的在真机上跑过一段范围 ✓（本轮 `region 8192 / coarse 4096 / slab 1024 / candidate 256` ✓）。
- `found=...` **不是**真对象 ✓：本轮**只有 2–4 个碰撞成员** ✓ ⇒ 规则太弱 ✓ ⇒ 探针**不声称**它 ✓（判据已改为「机器跑到了搜索阶段」✓）。
- **未做** ✗：同机旧二进制对拍（旁证口径 ✓）· 真碰撞数（需 profile 键 ✓ 已落 4 键 ✓）· 页准备与搜索在**同一次真机运行**里的串联（下一步 ✓）。

## 3 复现命令

```sh
make -C core snitch-landing-probe snitch-fork-probe snitch-run-probe
adb -s QV770MFGJ1 push build/native/<probe> /data/local/tmp/
adb -s QV770MFGJ1 shell 'timeout 40 /data/local/tmp/<probe>; echo EXIT=$?'
```
