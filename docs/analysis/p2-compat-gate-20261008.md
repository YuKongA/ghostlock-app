# P2 兼容面门禁证据（2026-10-08）

> **记录类**（≤8 KiB）。范围：**P2 新契约层的兼容门禁**（[contract-layer-p2-spec](contract-layer-p2-spec.md) §6 判据 ①–④）。
> 判据形式 = D1（黑盒/白盒 + 极端值）；**全部结果为本次原始复跑**（除标注「引述」者）。
> 上游：spec §6（判据）· [A1](contract-layer-p2-spec-annex-a1.md)（48 头全表）· [A2](contract-layer-p2-spec-annex-a2.md)（符号使用点）。

## §1 门禁① compat-check（NDK 解析 97 个 backend 文件）

- **实现位置**：`src/Makefile:17-27`（目标 `compat-check`；逐文件 `$(NDK_CXX) -std=c++23 -fno-rtti -Icore -isystem $(MPACK_DIR) -DTARGET_CONFIG_H=... -D__ARM=1 -fsyntax-only`；失败即打印 `COMPAT-FAIL: <file>` 并以非 0 退出）。
- **旗标含义**：`-fno-rtti` = 与生产一致；`-isystem lib/mpack` = 第三方头走 system；`-DTARGET_CONFIG_H="memory/target.h"` = target 配置注入点；`-D__ARM=1` = 目标架构分支。
- **命令**：`ANDROID_NDK_HOME=<ndk> make -C src compat-check`
- **原始输出**（尾部）：

  ```text
  compat-check: OK=97 FAIL=0 total=97 expected=97
  EXIT=0
  ```

- **计数口径**：`find src/core/backend -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) | wc -l` = **97**（= 门禁覆盖全集；其中**依赖上层头的子集 = 59**，见 A1）。`COMPAT-FAIL` 匹配数 = **0**。
- **幂等性**：同命令重复运行仍 `OK=97 FAIL=0`（本次复跑 2 次）。

## §2 门禁② C ABI 头可被 C 编译

- **命令**：`cc -x c -fsyntax-only src/core/contract/abi/glk_contract_abi.h`
- **结果**：**EXIT=0**（带 `-Icore` 亦为 0）⇒ 头内**无 C++ 构造**，C ABI 可用。

## §3 冻结量断言 + 独立 TU

| 冻结量 | 断言位置 | 值 |
|---|---|---|
| 后端槽 | `src/core/session/core_session.hpp:24` | `static_assert(kBackendStateBytes == 2048, "backend state slot size is frozen (P2 spec 2)")` |
| 槽内实际占用 | `src/core/backend/cve_2026_43499_state.hpp:41` | `static_assert(sizeof(Cve2026_43499State) <= kBackendStateBytes)`（实测 `sizeof = 1560`） |
| `kernel_offsets` | `src/core/contract/model.hpp:425` | `== 520` |
| `TargetProfile` | `src/core/contract/model.hpp:427` | `== 784` |

- **独立 TU 校验**（不依赖任何 backend 文件，证明断言在最小包含下即可编译）：

  ```sh
  printf '#include "contract/model.hpp"\nint main(){return 0;}\n' > /tmp/tu.cpp
  c++ -std=c++23 -fno-rtti -Isrc/core -isystem src/lib/mpack -fsyntax-only /tmp/tu.cpp
  # EXIT=0
  ```

## §4 门禁④ 证伪实验（**守卫必须能失败**）

- **做法**：把 2048 → 1024，观察断言是否失败。**实验在 `/tmp` 隔离副本进行**（`cp -R src /tmp/p2-falsify`）—— **不触碰** `src/core/**`（该写范围属 native-core，本件不越界）；因副本与原树同源，结论等价。
- **原始输出（节选）**：

  ```text
  core/session/core_session.hpp:24:38: note: expression evaluates to '1024 == 2048'
  core/session/core_session.hpp:24 | static_assert(kBackendStateBytes == 2048, "backend state slot size is frozen (P2 spec 2)");
  core/backend/cve_2026_43499_state.hpp:41:19: error: static assertion failed due to requirement
      'sizeof(ghostlock::backend::Cve2026_43499State) <= kBackendStateBytes':
      increase CoreSession::kBackendStateBytes
  core/backend/cve_2026_43499_state.hpp:41:46: note: expression evaluates to '1560 <= 1024'
  2 errors generated.
  FALSIFY-EXIT=1
  ```

- **结论**：门禁**能失败**（非 vacuous）：改小槽位即触发**两处**守卫 —— ① 槽尺寸冻结断言（`core_session.hpp:24`）② 后端状态**超出槽**（`cve_2026_43499_state.hpp:41`，实测 `1560 > 1024`）。

## §5 backend 零改动证明（硬判据：88 源文件一行不改）

```sh
git status --short -- src/core/backend   # 输出为空
git diff --stat  -- src/core/backend     # 输出为空
```

- 结论：**`src/core/backend/**` 零改动**（无修改、无新增、无删除）⇒ 兼容层无需改 backend 即满足 §1 门禁。

## §6 两条经验（如实记录，含可复现判据）

| # | 现象 | 机制 | 判据/规避 |
|---|---|---|---|
| **E-1** | **宿主 `c++` 假红**：同 97 文件用 host clang 编译 ⇒ `OK=87 FAIL=10`（引述：另一轮观测为 86/97，量级一致） | 头与 TU 是 **NDK 目标**（`-D__ARM=1`、`TARGET_CONFIG_H`、NDK libc++），宿主 SDK 缺目标分支/库 | **门禁必须用 NDK 工具链**；host 编译**不得**作为兼容判据（本次实测：host OK=87/FAIL=10 vs NDK OK=97/FAIL=0） |
| **E-2** | **写者活跃期瞬时假红**：同一条 compat-check 先 `FAIL=1` 后 `FAIL=0` | 门禁与 writer 并发 ⇒ 读到**半写文件** | 判据 = **成员状态**（`list_agents` 无 running 且写范围覆盖该文件）而非文件 mtime；出现此类红**不据此回退**，待 writer 停手后复跑 |

## §7 未决

| # | 未决项 | 现状（实测） | 处置 |
|---|---|---|---|
| U-1 | **compat-check 需显式 `ANDROID_NDK_HOME`** | 未设时：`compat-check: NDK C++ compiler not found: /toolchains/.../aarch64-linux-android35-clang++` + `make: *** [compat-check] Error 2`（**EXIT=2**；路径前缀为空 ⇒ `NDK_ROOT` 未解析） | 在 CI/脚本中**显式导出** `ANDROID_NDK_HOME`；或让 Makefile 从 `local.properties` 的 `ndk.dir` 回退解析 |
| U-2 | 门禁尚未纳入自动化门禁链 | 现为**手动**目标（`make -C src compat-check`） | 归 P2 收口批（与 `native-host-tests` 同批接线） |
| U-3 | 97 与「59 依赖上层」两个口径的关系 | 97 = backend 全集；59 = 依赖上层头的子集（A1） | 门禁**以 97 为准**（更严）；59 用于兼容层范围界定 |
