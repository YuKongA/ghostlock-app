# 附件 A3：include 闭包的 10 个传递头（交付集合 = **58**）

> 分析类（≤8192 B）。**配套**：[contract-layer-p2-spec](contract-layer-p2-spec.md)（§1 / §6）· [A1](contract-layer-p2-spec-annex-a1.md)（48 直接依赖头）· [A2](contract-layer-p2-spec-annex-a2.md)（符号使用点）。判据形式 = D1。
> **口径**：**交付头集合 == 58** = **48 个直接依赖头**（A1）+ **10 个 include 闭包必需的传递头**（本表）。依据：实现提交 **`97173d9f`**（"58 same-name headers"）。

## §A4 十个传递头（P2 交付面）

| # | 传递头 | 为何必需（闭包角色） |
|---|---|---|
| 1 | `contract/capability.hpp` | `capabilities.hpp` 的底层能力类型（能力声明面） |
| 2 | `contract/child_task.hpp` | root child / 子任务执行体类型（terminal 面依赖） |
| 3 | `contract/countermeasure.hpp` | 反制/缓解类型（plugin 与 backend 共用） |
| 4 | `contract/file_cache.hpp` | 文件缓存/落盘类型（handoff 与 LKM 装载路径） |
| 5 | `contract/kernel_alias.hpp` | 内核符号别名（KMI/符号解析面） |
| 6 | `contract/kernel_memory.hpp` | 内核内存视图/范围类型（写面与 geometry） |
| 7 | `memory/target_constants.hpp` | 目标几何常量（`TARGET_CONFIG_H` 的常量面） |
| 8 | `plugin/policy.hpp` | 插件策略类型（冻结期仅声明，运行时接线归 P5） |
| 9 | `support/native_result.hpp` | 中性结果类型（错误/状态传播） |
| 10 | `support/number_parse.h` | 数值解析（C 兼容头，供支持面复用） |

- **判据（硬）**：**交付头集合 == 58**（48 + 10）；**少一即某个 backend 文件编译失败**（由 `compat-check` 97/0 兜住）。
- **复核命令**：

  ```sh
  # 直接依赖头（A1 口径）：48
  python3 - <<'PY'
  import os, re, collections
  core="core/src"; dis=set()
  for root,_,fs in os.walk(core):
      for f in fs:
          if f.endswith(".disabled"): dis.add(os.path.relpath(os.path.join(root,f),core)[:-9])
  hdr=set(); pat=re.compile(r'#include\s*["<]([^">]+)[">]')
  for root,_,fs in os.walk(core+"/backend"):
      for f in fs:
          p=os.path.join(root,f)
          for m in pat.finditer(open(p,encoding="utf-8",errors="ignore").read()):
              for c in (m.group(1), os.path.normpath(os.path.join(os.path.dirname(os.path.relpath(p,core)),m.group(1)))):
                  if c in dis: hdr.add(c)
  print("direct:", len(hdr))   # 48
  PY
  ```
