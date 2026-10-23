# 大函数拆分计划（2026-10-21）

> 目标：把多 if/switch 的攻击函数拆成小函数；不改控制流、重试次数、所有权、join/close 顺序。
> 瀑布：本文件 → design-critic → 修订 → 逐批实现。
> 评审：2026-10-21 design-critic，技术阻断 0；可进 S1。
> S1 完成：run_exploit 拆分，flatten 19/19 PASS；证据：[s1-run-exploit-split](../analysis/s1-run-exploit-split.md)。
> S2 完成：do_tcp 拆分，flatten 11/11 PASS；证据：[s2-do-tcp-split](../analysis/s2-do-tcp-split.md)。
> S3 完成：do_pselect 拆分，flatten 4/4 PASS（ignore close）；证据：[s3-do-pselect-split](../analysis/s3-do-pselect-split.md)。
> S4 完成：prepare_kernel_page 拆分，flatten 17/17 PASS；证据：[s4-prepare-kernel-split](../analysis/s4-prepare-kernel-split.md)。
> S5 完成：prepare_skb_payload 拆分，flatten 0/0 PASS；证据：[s5-skb-payload-split](../analysis/s5-skb-payload-split.md)。
> S6 完成：child_main 拆分，flatten 24/24 PASS；证据：[s6-child-main-split](../analysis/s6-child-main-split.md)。
> S7a 完成：patch_file_cbc 拆分，flatten 10/10 PASS；证据：[s7a-cbc-split](../analysis/s7a-cbc-split.md)。
> S7b 完成：patch_hook/exploit 拆分，flatten 1/1、1/1 PASS；证据：[s7b-hook-exploit-split](../analysis/s7b-hook-exploit-split.md)。

## 清单（按分支数/指令数）

| 优先级 | 函数 | 位置 | 规模 |
|---|---|---|---|
| S1 | `run_exploit` | main.c | 376 行 / 69 分支 |
| S2 | `do_tcp_fake_lock_route` | fops.c | 170 行 / 20 分支 |
| S3 | `do_pselect_fake_lock_route` | fops.c | 148 行 / 13 分支 |
| S4 | `prepare_kernel_page` | util.c | ~200 行 |
| S5 | `prepare_skb_payload` | util.c | ~150 行 |
| S6 | `child_main` | main.c | 108 行 / 23 分支 |
| S7 | `exploit` / `patch_file_cbc` / `patch_hook` | 43284 exp.c | 56 / ~80 / ~70 行 |

`write_root_script` 的分支计数来自字符串字面量，不拆。

## S1：run_exploit

### 拆分

```c
struct exploit_chain {
  struct child_pipes pipes;
  pid_t child, parked_child;
  int parked_cmd_w;
  uintptr_t child_task;
  int child_alive, seccomp_ok, ever_rooted;
};

enum w1_result { W1_CONTINUE, W1_DONE_OK, W1_FAIL };
enum chain_result { CHAIN_W2_FAIL, CHAIN_W3_DONE };

static int exploit_prelude(Glk43499Session *s);
static int run_phase1_probe(Glk43499Session *s);      /* -1 not requested, 0/1 result */
static enum w1_result run_w1_stage(Glk43499Session *s);
static int run_vr_tag_clear(Glk43499Session *s, uintptr_t child_task);
static int run_w3_attempts(Glk43499Session *s, struct exploit_chain *c,
                           const struct w3_stage_context *w3);
static int run_w2_w3_chain(Glk43499Session *s, struct exploit_chain *c);
static int run_post_root(Glk43499Session *s, struct exploit_chain *c);
```

`run_exploit` 保留为：

```text
prelude
p = phase1_probe; if (p >= 0) return p;   /* 成功/失败都立即返回 */
w1 = run_w1_stage; if (W1_FAIL) return 1; if (W1_DONE_OK) return 0;
chain = run_w2_w3_chain; if (CHAIN_W2_FAIL) return 1;
if (run_post_root(s, &chain)) return 1;   /* 当前路径：不 stop */
kernel5_resident_stop(s); return 0;
```

`run_post_root` 返回 1 表示 `!ever_rooted`（当前在 sleep 后判定）；
`run_exploit` 直接返回 1，不执行末尾 `kernel5_resident_stop`，保持当前行为。

### 保持

- 所有 `TIMER` 文本与位置
- W1 尝试次数 15/1、W1b 3 次、W3 6 次、chain 3 轮
- `slab_drain` 时机
- `pipes` 的 close/waitpid 顺序
- `vr_needed` 探测与 tagA/tagB 写入
- 失败返回 1 的位置

### 风险

- `run_exploit` 现在有早返回；phase1 必须显式 `p>=0` 返回，不能继续 W1。
- W1 三态：already permissive→CONTINUE；W1_ONLY→DONE_OK；失败→FAIL。
- chain 两态：W2_FAIL 不执行 post；W3_DONE 才执行 post。
- W2/W3 chain 用局部 `exploit_chain`，增加 `chain_result`；不变成全局。
- 明确六条 cleanup 路径，保持当前 close/waitpid/stop（含当前不关的分支）：

| 路径 | 当前行为 |
|---|---|
| W2_FAIL | write X；close cmd_w/uid_r；waitpid WNOHANG；return 1 |
| spawn 失败 | return 1（保持现有） |
| child_task=0 | kill(-child)；waitpid；child_alive=0；close cmd_w/uid_r；continue |
| W3_LOST | child_alive=0；break；下一轮 top 关 cmd_w/uid_r |
| W3_DONE | post 按 child_alive/parked 关 cmd_w 或 parked_cmd_w；最后 close uid_r |
| W1_FAIL | 写失败：stop resident → return 1；W1b 失败：return 1（不 stop）；resident 修复失败：stop → return 1；W1_ONLY：DONE_OK，不 stop/不 chain/不 post |

- `run_w2_w3_chain` 用单一 cleanup 出口，但不得把当前“不关”的分支改成关闭
  （S1-04 要求保持，不是清理）。

## S2：do_tcp

拆分：

```text
tcp_validate_payload
tcp_arm_consumer
tcp_send_stamp
tcp_wait_route
tcp_verify_result
```

保持 socket 创建顺序、`SO_SNDBUF`、stamp 写入顺序、join 顺序。
`do_tcp_fake_lock_route` 只做参数检查 + 顺序调用。

## S3：do_pselect

拆分：

```text
pselect_build_fdsets（已有 prepare_pselect_fdsets）
pselect_arm_pipes
pselect_wait
pselect_verify
```

保持 FD_SET 顺序、`pselect` 参数、`standard_io` 备份/恢复、close 顺序。

## S4/S5

`prepare_kernel_page`：

```text
prepare_cleanup_previous
prepare_spray_contexts
prepare_leak_child
prepare_reclaim_sockets
prepare_skb
```

`prepare_skb_payload`：

```text
payload_geometry
payload_fake_lock
payload_fake_task
payload_pselect_words
payload_cred_copy
```

不改变 offset 计算与写入顺序。

## 门禁（每批）

```text
./gradlew :app:externalNativeBuildDebug --offline   # 0 warnings
cmp_disasm --important-only --important-ignore-new    # 无 COUNT/ORDER FAIL
llvm-nm b/B = 0
```

`cmp_disasm --count-delta` 会因新增 helper 的 `bl` 变红；不作为拆分判据。

S1-05：新增 helper 后，`run_exploit` 的重要 syscall 会搬到新符号，
`--important-ignore-new` 会漏算。S1 判据工具 **已实现**：`tools/important_flatten.py`；
对未拆分二进制自检 `FLAT-SUMMARY base=19 cur=19`/`PASS`。

```text
输入：parent=run_exploit，helper 列表（S1 的 7 个）
处理：反汇编 parent；遇到 bl helper 时递归展开 helper 的 bl/syscall 序列
输出：扁平 important call/syscall 序列（order+args），与 baseline run_exploit 对比
```

- **判据**：`FLAT-COUNT`/`FLAT-ORDER` 必须 PASS；`args_diff` 只报告，
  由人工逐条归因（移动代码会改变 frame/register provenance），不作为 PASS 条件。
- `--important-ignore-new` 只作辅助。
- 若无法展开，退回“important syscall 保留在 run_exploit 包装函数内”的方案。
- S1 门禁命令（7 个 helper）：

```text
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent run_exploit \
  --helper exploit_prelude --helper run_phase1_probe \
  --helper run_w1_stage --helper run_vr_tag_clear \
  --helper run_w3_attempts --helper run_w2_w3_chain \
  --helper run_post_root
```

- 未拆分二进制自检：`base=19 cur=19 args_diff=1 PASS`（同一 baseline/candidate 的已知 arg 差）。
- 每批记录 important 函数数、base/cur、扁平序列 diff。

## 批次顺序

1. S1 run_exploit（最大，先做）
2. S2 do_tcp
3. S3 do_pselect
4. S4/S5 prepare
5. S6 child_main
6. S7 43284

每批单独验证；S1 未过 important 不进 S2。
