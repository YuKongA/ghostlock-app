# 计划：删我写的 43499 核心攻击代码，搬旧树 6 个目标函数接入架构（2026-10-12）

## 0. 用户裁定（逐字）

> 「你写的43499实现到现在还是不工作，把你的核心攻击代码删掉，把旧的cmp_disasm的几个目标核心函数复制过来接入架构，此为下一步计划」

⇒ 读作三条：① 删**核心攻击代码**（不是整模块）；② 把 `cmp_disasm` 的目标函数从**旧树**复制过来；③ **接入**新架构。

## 1. 目标函数清单（权威 = tools/cmp_disasm.py 的 TARGETS，:79-146）

```

该工具头部注释逐字：「Compare the disassembly of attack-critical functions between two native binaries.」
TARGETS 共 6 条（每条含旧的 demangled 拼写与命名空间限定拼写）：

  1  owner_thread(void*)                                  旧树 race/threads.{hpp,cpp}
  2  waiter_thread(void*)                                 旧树 race/threads.{hpp,cpp}
  3  consumer_thread(void*)                               旧树 race/threads.{hpp,cpp}
  4  run_main_route_threads(WriteRequest const& / const*) 旧树 route/route_middleware.cpp + route/multicast_waiter_route.cpp
  5  do_kernel5_fake_lock_route(WriteRequest const*)      旧树 route/multicast_waiter_route.cpp (+ route_policy.hpp, route_api.hpp)
  6  do_one_write(WriteRequest const*, char const*)       旧树 attack/ops.cpp（声明在 session/backend/cve_2026_43499_backend.hpp）
     别名极多：attack::do_one_write · ops::do_one_write ·
     Cve43499Primitives::attack_write<MulticastPolicy/SelectPolicy/TcpPolicy> ·
     Cve2026_43499Policy::attack_write<...> · session::backend::Cve2026_43499Policy::attack_write<...> ·
     session::ExploitProcedure::attack_write(...)

（工具 :522 还有一条 "and branch target unchanged (T3c C++ bool)"，说明它带 T3c 分支目标判据。）

```

## 2. 两侧规模（已实测）

```

旧树 ../ghostlock-app-ref/src/core      89 文件 / 10148 行
新树 core/src/backend/cve_2026_43499    102 文件 / 11051 行
  逐模块：memory 2297 · leak 2172 · landing 2232 · steps 1920 · route 510 · race 455
=> 两侧规模几乎相等 —— 所以问题不在体量，在"能不能工作"。用户判定新树的不工作，故取旧树的这 6 个函数。

```

## 3. 步骤（严格按此序，不跳步）

```

第 0 步 勘察（不写码）：
  a 只读导出旧树这 6 个函数的完整实现，及其直接依赖（私有类型/常量/助手）清单
  b 对照新架构接口面（memory::WriteRequest · ExploitSession · 三个 route policy · 沙箱步入口 · access_params）
  c 列出"新树已有对应"与"需要一起搬的最小集"
  d 产出删除清单：新树里与这 6 个函数功能重叠的文件逐条列出（file:line + 是否被别处引用）

第 1 步 删除（只删核心攻击实现，不删被别处依赖的 ABI/route/access）：
  · 每条删除前核引用面；被 main.cpp 入口、steps 注册、Makefile 编译行引用的，同批改
  · 保留下来的：ABI 偏移读取（layout_from_document/access_params）、route 接口、access 链

第 2 步 搬入：
  · 6 个函数按旧树实现**逐行**搬进新架构；接口处做最小适配
  · 不重写、不"优化"、不改算法、不改常量 —— 搬入的意义就是行为等同

第 3 步 判据（缺一不可）：
  a 编译：make -C core -j1 check => 109 PASS 起 / 0 FAIL；-B android => 0 error / 0 warning
  b cmp_disasm：6 个目标逐个判定，先跑一次拿基线档位（strict / layout / operands 三档，:11-23 定义）
  c 真机（-s QV770MFGJ1，推前先 -B android）：rc 与 param-invalid=0 / w1 => won / exit-code 末行
  d 归档 docs/analysis/device-gates/

```

## 4. 要用户一句话的一点（范围）

```

第 1 步的删除范围：
  (甲) 只删与这 6 个函数功能重叠的实现（memory/leak/landing/steps 里的攻击实现），保留 route 与 ABI 链
  (乙) 删整个 core/src/backend/cve_2026_43499/**，连 ABI/route 一起重来
用户原话是"把你的核心攻击代码删掉" ⇒ 字面支持 (甲)。
=> 我按 (甲) 拟，但删之前会把每条删除的引用面列给你看，不先斩后奏。

```

## 5. 与 P4 的关系（必须先声明，避免重演上一轮的错误）

```

docs/analysis/config-wire-redesign-plan.md §9 P4 逐字：删 mpack/Document/派发/plan_gate/profile-core；净删。
本计划不触碰 P4 的删除面（glkv3_document · plan_gate · profile-core · mpack 残留 · HOCON 归档）。
两件事并行但互不重叠：P4 是"删旧的"，本计划是"把旧树 6 个函数搬进新树并接上"。
若两者在同一文件上相遇（例如 route/ 与 派发），以本计划的"搬入"为先，P4 的删除排在其后 —— 这需要用户确认。
## 6. 勘察结论（明细见附件）

搬迁面约 2400 行、依赖归类、删除次序、判据 b 的确切口径 —— 全部移到附件：
43499-port-six-targets-annex-scope.md（3679 B）。

本节的结论两条：
  · 删除次序：边缘件（引用 <= 2）可先删；枢纽件（引用 5-7，含 landing/multicast_route.hpp）必须替换式，
    否则出现编不过的中间态。
  · 判据 b 的硬约束：cmp_disasm 的 MISSING 也算失败 => 不得改函数名、不得进匿名命名空间。
## 7. 仍待用户裁的两条（第 1、2 步开工前置）

① 搬入范围：按第 6 节的量化，(甲) 搬 10 个文件 + 另补 8 件 是"搬一整层"的最小完整形态；
   若只搬 6 个函数而不补那 8 件，删完编不过（勘察 d-1 的结论）。

## 8. baseline 与 candidate 的取得（2026-10-12 查实）

```

cmp_disasm 用法（工具 :548-557）：cmp_disasm.py [--reviewed] <baseline> <candidate>

baseline（旧树二进制）—— 路径已明确，且独立于 mpack：
  旧树构建入口 src/Makefile（另有 src/CMakeLists.txt）；目标 all: ghostlock(:86) / ghostlock(:88)
  grep -c mpack ../ghostlock-app-ref/src/Makefile => 0    => 不受用户删 mpack 的影响
  命令：make -C ../ghostlock-app-ref/src ghostlock
  注意：那是一棵独立 worktree，构建会写它自己的 build 目录；未获用户同意前不擅自跑。

candidate（新树二进制）—— 缺件，且是整条链现在唯一的缺件：
  新树当前编不过（clang: no such file or directory: lib/mpack/mpack.c）
  它的恢复取决于两条仍待裁的裁定：P4 批次顺序、plan_gate/stage_gate 处置。

新增两步（插进第 3 节顺序）：
  第 0.5 步 先把新树恢复到可构建（否则搬入后连"能不能编过"都测不了）
  第 2.5 步 在旧树 worktree 构建一次，取得 baseline（需用户点头）

```

② 搬入后 plan_gate / stage_gate 的处置（旧 6 函数是直连调用；新树是沙箱录制 -> gate 准入 -> 按相位执行；
   P4 又把这两个 gate 列为待删 => 两处裁定互斥）：旁路 / 删除 / 保留，三选一。

```

## 9. 清理对象的两类分野（2026-10-12 核清，避免误删）

```
甲类 该清的（计划说删/停，树里还在，且无登记条件）
  profile-core 整模块（app/build.gradle.kts:31 仍读其 manifest）
  plan_gate（计划称「不再需要预检」，而 core 非测试 6 文件在用）
  glkv3_document + mpack 链（P4 删除清单；现树因此编不过）
  113 件 *.disabled（git 跟踪 0）· core/cmake-build-debug
  run_pipeline_temp.{hpp,cpp}（删除须按其登记条件，见乙类）

乙类 登记在、等条件的（不得当垃圾删）
  run_pipeline_temp：登记于 docs/analysis/device-gates/20261009-1621-temp-entry-steps.md:3
    （经 --ghostlock-app-call 启动，符合 AGENTS §2 的例外条款）；
    清理表与前置见 docs/analysis/pending-rulings.md:75（需设备 + SA 材料，两者不在仓库内）
  43284/session_frame.cpp:5/:7 与 43284/ipsec/aes256.cpp:232：
    逐字含 TODO(ipsec-not-ported) 与 Removal condition（等 IPsec 层落地才可删）

  登记分散在五处：源码 TODO(reason) · analysis/*-checklist.md · device-gates/ ·
  pending-rulings.md · MASTER-PLAN.md。清点时须多路查，不要只查 docs/plan。
```
