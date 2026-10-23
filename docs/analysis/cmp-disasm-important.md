# cmp_disasm 重要调用/参数链分析（2026-10-21）

> 工具：`tools/cmp_disasm.py` + `tools/important_trace.py`。本功能**保留** strict/reviewed/partial/count-delta 全部旧模式；只在加 `--important` 时运行。

## 用法

```sh
# 旧比较 + 重要调用分析
python3 tools/cmp_disasm.py --reviewed --count-delta 32 --important \
  BASELINE CANDIDATE

# 只做重要调用分析（不跑旧 TARGETS）
python3 tools/cmp_disasm.py --important-only --important-json out.json \
  BASELINE CANDIDATE

# 打印每个参数的完整链
python3 tools/cmp_disasm.py --important-only --important-verbose \
  BASELINE CANDIDATE
```

可选：

| 参数 | 作用 |
|---|---|
| `--important-json PATH` | 双份分析写入 JSON |
| `--important-symbols FILE` | 追加重要函数名（每行一个，`#` 注释） |
| `--trace-depth N` | 反向追踪深度，默认 64 |
| `--trace-indirect` | 也追踪 `blr` |
| `--important-ignore-new` | 只出现在 candidate 的函数记为 OK(new)，不判失败 |
| `--important-ignore-removed` | 只出现在 baseline 的函数记为 OK(removed)，不判失败 |

## 重要指令

- 直接 `bl <symbol>`：symbol 在默认集合或 `--important-symbols` 中。
- `svc #0`：始终记录，参数 `x0..x5` + `x8`（syscall 号）。
- `blr`：仅 `--trace-indirect` 时记录，目标为寄存器/符号。

默认集合覆盖：

```text
futex_op / sched_setattr_tid / pin_to_core / pthread_create / pthread_join
socket / setsockopt / getsockopt / sendmsg / splice / vmsplice / mmap / ioctl
open / openat / close / read / write / clone / fork / execve / waitpid
kill / tgkill / syscall / prctl / memfd_create / pselect / select / fcntl
sched_setaffinity / perf_event_open / init_module / finit_module ...
```

每条记录：

```text
函数、序号、偏移、kind(call/syscall/indirect-call)、target、参数表
```

参数个数按函数表裁剪，例如：

```text
close x0；getuid 无参数；pthread_create x0..x3；futex_op x0..x5；syscall x0..x6
```

## 参数链

`trace_register(instrs, index, reg)` 从重要指令前一条开始反向扫描，遇到写该寄存器的指令就记录一步，并递归追踪其源寄存器。

根类型：

| kind | 含义 |
|---|---|
| `arg` | 函数入口参数 / 未定义来源 |
| `fixed` | `movz/movn/movk/adr/adrp` 立即数或地址 |
| `frame` / `frame-load` | `sp` / `x29` 栈帧或栈槽 |
| `load` | 内存加载（全局/结构体） |
| `computed` | `add/sub/and/orr/eor/lsl/...` 等计算 |
| `call-return` | `bl` 返回值（x0） |
| `syscall-return` | `svc` 返回值（x0） |
| `system-register` | `mrs` |
| `unknown` | 无法归类/深度限制/循环 |

每一步保留：

```json
{"offset": "0xd520", "text": "mov w8, #0xb2"}
```

示例：

```text
IMPORTANT-TREE waiter_thread #2 call:syscall@plt @0xd528
  x0 <-
    fixed:0xb2
      0xd520: mov w8, #0xb2
      0xd524: mov w0, w8
  x1 <- arg:x1
  ...
```

## JSON 结构

```json
{
  "baseline": {
    "binary": "path",
    "functions": {
      "waiter_thread": [
        {
          "index": 2,
          "offset": "0xd528",
          "kind": "call",
          "target": "syscall@plt",
          "args": {
            "x0": {
              "kind": "fixed",
              "value": "0xb2",
              "steps": [{"offset": "0xd520", "text": "mov w8, #0xb2"}],
              "children": []
            }
          }
        }
      ]
    }
  },
  "candidate": { "binary": "path", "functions": {} }
}
```

## 归一化规则

- `adrp/adr` + `add/sub #imm`：作为地址链，忽略页面/偏移重划分；比较只看向对地址链是否同形。
- `sp/x29` 栈帧：偏移归一化为 `#0xH`，避免编译器栈布局变化导致假差异。
- 全局/结构体 `load [x8,#off]`：若基址来自 adrp 链，偏移归一化；否则保留精确 offset，交人工判断。
- 绝对地址、符号注解：按 strict 规则归一化。
- 参数个数按函数表裁剪，避免把 `close` 的 x4-x7 残值当参数。

## 验证输出

`compare()` 逐函数输出：

```text
IMPORTANT-SUMMARY functions=N base=M cur=M
IMPORTANT-COUNT <fn>: base=N cur=M OK/FAIL
IMPORTANT-ORDER <fn> #i kind:target OK/FAIL
IMPORTANT-TRACE <fn> #i arg xN OK/DIFF base=<summary> cur=<summary>
IMPORTANT-RESULT: PASS/FAIL
```

当前 P1b-2a 对 baseline 的实测（43499）：加 `--important-ignore-new` 后 **9 条 DIFF**（不加则 12 条，含新函数 `run_with_session`），均可解释；43284 同批为 12 函数 / 52 条重要指令，**PASS**。

- `run_main_route_threads` pthread_create 参数由 NULL 改为 session/arg（P1b-2a 的真实接口变化）。
- `do_tcp`/prepare 的全局/结构体 load offset 随布局变化。
- 新函数 `run_with_session` 只在一侧（base=0 cur=1）。
- 栈帧差异已归一化，不再计入。

## 已知边界

- 反向线性扫描，不区分控制流路径、循环、异常边；同一寄存器在不同路径的值可能合并。
- 不跨函数做别名/返回值形状分析；`call-return` 只记符号，不展开被调函数。
- `ldp` 目前只按第一个内存操作数记一个 load；两个目的寄存器共享同一描述。
- SIMD/浮点参数不追踪；只覆盖整数 x0-x7。
- `blr` 默认关闭；打开后目标寄存器会进入 children。
- 归一化会隐藏“同一链但偏移变化”的差异；若偏移本身是语义参数，用 `--important-verbose` 看原始 `steps` 文本。

## 测试

```sh
python3 tools/tests/test_cmp_disasm.py       # 17
python3 tools/tests/test_important_trace.py  # 17
```
