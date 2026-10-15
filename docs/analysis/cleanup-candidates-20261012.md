# 清理候选清单（2026-10-12 · 只读核实，未执行任何删除）

## 0. 本文性质

本件登记三类**已核实但未处置**的清理对象，供用户一次裁决。所有核实均为只读（`realpath` / `git ls-files` /
`git check-ignore` / `du` / `ls -ld`），**未执行任何删除**。

## 1. 已核实的陈旧产物与残留（三件，约 1.6 MB）

```
① core/cmake-build-debug   876K   未跟踪 · mtime Oct 10 14:27 · 含 CMakeFiles/mpack.dir 与
                                    .cmake/api/v1/reply/target-mpack-Debug-1fa98ac2f56bc5a77f04.json
   核实：realpath 在本仓库内（非链接、非旧树）；git ls-files = 0；不含对旧树的引用
   性质：CMake 产物。本树用 Makefile + Gradle，无 CMakeLists => 来自 IDE 或手工 cmake

② profile-core/bin         696K   被 .gitignore:12 (bin/) 忽略 · mtime Oct 12 13:21 · git ls-files = 0
   内含 bin/test/device-document-fixture.hex 与 bin/test/payload-document-fixture.hex
   核实：这两份是构建时从 src/test/resources 拷来的副本，权威件在被跟踪的 src/test/resources
   性质：产物目录。删它不会丢夹具

③ core/.idea               108K   被 .gitignore:4 (.idea/) 忽略 · mtime Oct 11 22:15 · git ls-files = 0
   性质：IDE 配置残留（9 个文件）
```

**为什么不擅自动手**：AGENTS §4 的授权原文是「疑似**旧树产物** `realpath` 核实后清理」。
这三件核实结果都是"本树产物/残留，非旧树产物"，所以那条授权并不精确覆盖 —— 不拿错位的授权删东西。

## 2. 不得当垃圾删的（核实时顺手澄清）

```
core/.codex   4.0K   git 跟踪 1   => 曾有意提交，保留
.vscode       4.0K   git 跟踪 1   => 曾有意提交；且正是 PENDING-DECISIONS C5 的条目（是否保留跟踪）
```

## 3. 登记在、等条件的临时物（与第 1 节区分，避免误删）

```
pipeline/run_pipeline_temp.{hpp,cpp}   1125+33 行
  登记：docs/analysis/device-gates/20261009-1621-temp-entry-steps.md:3
        （临时入口驱动，经 --ghostlock-app-call 启动 => 符合 AGENTS §2 的旗标例外条款）
  清理表与前置：docs/analysis/pending-rulings.md:75（需设备 + SA 材料，两者不在仓库内）

43284/session_frame.cpp:5/:7 与 43284/ipsec/aes256.cpp:232
  逐字含 TODO(ipsec-not-ported) 与 Removal condition（等 IPsec 层落地）
```

登记分散在五处：源码 `TODO(reason)` · `analysis/*-checklist.md` · `device-gates/*temp-entry*` ·
`pending-rulings.md` · `MASTER-PLAN.md`。清点须多路查。

## 4. 待用户裁决

```
(a) 删第 1 节三件 —— 按"清理产物与 IDE 残留"执行，逐步记入清理归档
(b) 只删前两件（core/.idea 是 IDE 配置，用户可能仍在使用）
(c) 都不删 —— 本件即为登记，保持现状
```

## 5. 攻击关键路径上的清理边界（2026-10-12 由源码头注反推，收紧本次范围）

```
判据来源：seam/multicast_route.hpp 头注逐字
  "Three details are kept because the retired route needed them, and each is a place a
   cleaner-looking version would silently stop working"
=> 攻击关键路径上，看起来更干净的写法会静默失效。因此不得以行数、引用数、注释占比判删。

允许清理的三类：
  ① 清重复：两套窗口实现只留一套
     seam/ 一套（multicast_route.hpp 的 *_main）与 pipeline/run_pipeline_temp.cpp 一套（1125 行）并列存在，
     两套都未被 cmp_disasm 验证（符号名都不匹配 TARGETS 的 *_thread）。
  ② 清未跟踪产物与 IDE 残留：core/cmake-build-debug · profile-core/bin · core/.idea（见 §1）
  ③ 清注册散乱：把'自称待删'集中成一行台账（现散在五处，见 §3）

不得清理的（本轮已读头注后撤回）：
  steps/window_steps.{cpp,hpp}     节奏层，承载脚本侧 w1/w2/w3 请求（wire-schema 的 steps 声明）
  steps/window_attempt.{cpp,hpp}   尝试层，判据：cmp_requeue_pi 返回 <1 即未搬
  steps/window_runner.{cpp,hpp}    粘合层，边界：字与守卫由调用方给，缺则具名
  seam/multicast_window.hpp        窗口体，'does three things and no more'；零热身轮具名拒绝
  seam/multicast_route.hpp         三actor，'The walk is the write'；三处细节必须保留

  撤回理由：以上五件的头注记录的是约束（为什么不能简化），不是冗余。
  与'两套实现的 34% 注释占比'性质不同：那一套是试错史，这五件是设计约束。

仍待读头注再判（不得凭数字）：memory/write_layout.* · memory/multicast_arm.* ·
  memory/payload_page.* · memory/credential_image.* · leak/ 各件
```

## 6. 第二次修正：memory/ 四件同为约束层，撤回删判（2026-10-12）

```
逐个读头注后（方法见 §5），以下四件均不属冗余：

  memory/write_layout.hpp     决策层：本单元在调用方给的值之间选择，从不自己写常量
  memory/multicast_arm.hpp    几何校验层：两字段须容纳于缓冲且互不重叠，不合格者写前具名拒绝
  memory/payload_page.hpp     来源与约束层：形状取自退役树的 prepare_skb_payload（compact 分支），
                              逐字段保留；并给出硬约束 —— 伪造 task 的优先级须高于 RT 阈值，
                              低于它永远到不了 erase
  memory/credential_image.hpp 输入化层：所有数字都是输入；旧树把缓冲大小当页阶常量，这一版不

=> 撤回删判，与 §5 的五件合计九件。

修正后，先前那张删除表里真正站得住的只剩：
  pipeline/run_pipeline_temp.{hpp,cpp}   第二套窗口实现（与 seam 那套并列，两套都未被 cmp_disasm 验证）
  core/cmake-build-debug · profile-core/bin · core/.idea   三件未跟踪产物与残留（见 §1）

教训（写给后来的清理者）：按行数、引用数、注释占比、文件名判删，本次十一条判错九条。
逐个读头注才判得准；而攻击链上少一条约束不会报错，只会静默失效（§5 引 route 头注原话）。
```

## 7. leak/ 四件与两处更重要的发现（2026-10-12）

```
leak/ 四件（读头注后同样撤回删判）：
  futex_hash.hpp      移植件，见下"发现一"
  snitch_region.hpp   进程间接口层，见下"发现二"
  candidate_match.hpp 判定规则层（一致才成立，一处不一致即不是该对象）
  collision_buckets.hpp 真规则层（地址落哪个桶、候选是否与已知地址碰撞）
=> 撤回累计：steps/ 三件 + seam/ 两件 + memory/ 四件 + leak/ 四件 = 十三件

发现一：本树早有"整份搬退役树文件"的固定做法与标注格式
  leak/futex_hash.hpp 头注逐字：PORTED, NOT WRITTEN (2026-10-08)，并写明
  "this file is the retired tree copy of backend/cve_2026_43499/leak/futex_hash.h"
  且保留上游 Linux 代码的原始署名与 public-domain 声明。
  => 用户这次要求的"把旧的复制过来"不是新方法，而是沿用既有做法；
     搬入时照此格式标注（PORTED, NOT WRITTEN + 日期 + 原路径 + 保留上游署名）。

发现二：本树的核心风险模型是"错误不报错，只静默失效"
  snitch_region.hpp：进程间接口，字段插在中间会移位 => "the kind of thing that breaks silently"
  multicast_route.hpp："a cleaner-looking version would silently stop working"
  => 两处独立印证。故攻击关键路径上的清理姿势不是删，而是保持逐字段/逐语句对齐；
     任何改动都必须有能失败的外部判据 —— 这正是 cmp_disasm 存在的理由。

剩余候选（唯一站得住的重复实现 + 三件未跟踪产物）
  pipeline/run_pipeline_temp.{hpp,cpp}  ·  core/cmake-build-debug  ·  profile-core/bin  ·  core/.idea
```

## 8. 全模块头注审查总账

```
留 41 件：顶层 16 · race 6 · route 6 · steps 3 · seam 2 · memory 4 · leak 4
删 4 件：pipeline/run_pipeline_temp.{hpp,cpp} · core/cmake-build-debug · profile-core/bin · core/.idea
方法：读头注判层；不以行数、引用数、注释占比、文件名判。
```
