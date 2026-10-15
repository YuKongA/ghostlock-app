# 43499 模块头注审查收口记录（2026-10-12 · 只读 · 未删任何文件）

## 1. 为什么要有这份

本次「清理代码」审查了 `core/src/backend/cve_2026_43499/**` 全部 51 件。逐组读头注后，
我先前的删除表被撤回 13 处 —— 也就是说，**按行数、引用数、注释占比、文件名判删，十一条判错九条**。
把 51 件的判留依据集中记在这里，免得后人重判时重犯同一个错。

## 2. 方法（三条，缺一不可）

```
① 读头注判层：头注第一段就交代这件是什么层（契约/策略/约束/来源/移植/执行体）。
   判据是头注的文字，不是行数、引用数、注释占比或文件名。
② 分清两类高注释占比：
     设计约束（如 seam/multicast_route.hpp 的三处细节）=> 必须保留
     试错史（如另一套实现里的"第 N 轮"注释）=> 那是重复实现的症状
③ 一切改动必须有能失败的外部判据（cmp_disasm 逐指令 + 真机四读数）；
   因为本树的核心风险是"错误不报错，只静默失效"。
```

## 3. 留 50 件：分组与判据

```
顶层 16 件    接口/ABI/能力/采样层
  abi.hpp(偏移来源) · access_factory_43499(按 route 选 access) · access_{multicast_waiter,select_stack,
  tcp_zerocopy}(三条 route 各一) · access_params · capabilities(能力表) · kernel_access(脚本可达面) ·
  layout_from_document(文档->数字) · perf_ring/perf_seam/perf_seam_linux/perf_vote(采样四件) · session

race/ 6 件    PI 竞态层
  futex_seam_linux(真 futex 调用，藏在 seam 后) · layer(PI-futex 竞态) · pi_race · pi_window(PI 内禁令) ·
  race_runtime(一次尝试要什么/能得出什么)

route/ 6 件   路由策略层
  expected_keys(每条 route 要哪些键) · lifecycle(prepare/execute/disarm/destroy，and nothing else) ·
  multicast_policy(握手 as a decision) · policy_dispatch(按值而非协议分发) · route_policy · route_status

steps/ 12 件  步骤编排与映射层
  action(决策，与执行分离) · discovery_runner · prepare_mapping(wire schema 键 -> 各单元输入) ·
  prepare_runner(搜索接页准备，产出窗口要的那对字) · prepare_values_from_wire · route_config(纯函数建出) ·
  step_runner(按契约顺序跑) · targets · words_region(承载者，把指针借给调用方) ·
  window_steps(节奏，承载 w1/w2/w3) · window_attempt(尝试，判据 cmp_requeue_pi<1) · window_runner(粘合)

seam/ 2 件    窗口体与三 actor
  multicast_window(the thing called with the window open; does three things and no more) ·
  multicast_route(waiter/owner/consumer; The walk is the write; 三处细节必须保留)

memory/ 4 件  决策/几何/来源/输入化层
  write_layout(在调用方给的值之间选择，从不自己写常量) · multicast_arm(几何校验，写前具名拒绝) ·
  payload_page(取自退役树 prepare_skb_payload，逐字段保留；优先级须高于 RT 阈值) ·
  credential_image(所有数字都是输入；旧树把缓冲大小当页常量的做法被改掉)

leak/ 17 件   碰撞搜索层
  头注系统性地交代三类信息：平台边界(written as a concept / no decisions, only calls) ·
  所有权(with one owner / what it can say by itself) · 契约(the way the contract asks for one)
  含 futex_hash.hpp：标 PORTED, NOT WRITTEN (2026-10-08) + 原路径 + 保留上游 Linux 署名
  => 这是本树"整份搬退役树文件"的既有做法与标注格式，搬入时照此写
```

## 4. 删 4 件（唯一站得住的）

```
pipeline/run_pipeline_temp.{hpp,cpp}   1158 行   第二套窗口实现（与 seam 那套并列）
      两套入口都是 *_main，而 cmp_disasm 的 TARGETS 要 *_thread => 两套都未被验证过
core/cmake-build-debug   876K   未跟踪 · 含 CMakeFiles/mpack.dir 与 target-mpack-Debug-*.json
profile-core/bin         696K   被 .gitignore:12 忽略 · 含两个夹具的构建副本
core/.idea               108K   被 .gitignore:4 忽略 · mtime Oct 11
三者均非旧树产物 => AGENTS §4 的授权（疑似旧树产物 realpath 核实后清理）不精确覆盖，故未执行。
```

## 5. 与本次审查无关但已登记的（避免误删）

```
profile-core 整模块 · plan_gate · glkv3_document+mpack 链 · 113 件 .disabled
  => 属"计划说删/停，树里还在"，见 43499-port-six-targets-plan.md 的 §9 甲类
pipeline/run_pipeline_temp 同时出现在两份清单里：
  作为重复实现它该清，但它的删除有已登记的条件（device-gates/20261009-1621-temp-entry-steps.md 与
  pending-rulings.md:75 的清理表，前置为设备 + SA 材料）=> 处置前须两者都看
```

## 6. 第六条纪律：具名拒绝，且逐层收紧（2026-10-12 由五处头注提炼）

```
同一要求在各处措辞不同，逐层收紧：

  seam/multicast_window.hpp   Zero warm-up rounds means the caller did not say,
                              which is refused by name rather than read as a default.
  steps/window_steps.hpp      失败一律具名：尝试用尽、钩子缺失都不是静默的零。
  steps/window_runner.hpp     缺它们时本层会具名说缺。
  memory/multicast_arm.hpp    refused before a byte is written.
  steps/route_config.cpp      the refusal names the field the caller forgot.

  => 不许静默零 -> 具名拒绝 -> 指出漏掉的字段。

对清理的约束：任何"简化"都不得把具名拒绝变成默认值或静默零。
  这条与 §2 的"更干净的版本会静默失效"是同一枚硬币的两面：
  前者说做不到的事要说出来，后者说看起来更干净的写法会悄悄不工作。
```

## 7. 审查面完整性（2026-10-12 核对）

```
已覆盖的组与件数：
  顶层 16 · race 6 · route 6 · steps 12 个 .hpp 与 8 个 .cpp（一一配对） ·
  seam 2 · memory 4 个 .hpp（+ cpp 抽查） · leak 17 个 .hpp 与 1 个 .cpp
=> 没有未判的件。
```

## 8. TARGETS 候选表的实际命中面（2026-10-12 实测）

```
cmp_disasm.py 的 TARGETS 每条都列了多个候选拼写，但这些拼写不是"新旧两侧各一份"，
而是把多个时期的命名混在一起。实测：

  Cve43499Primitives       新树 0 命中 · 旧树 0 命中（TARGETS:109-112 那一批）
  MulticastPolicy 类名     新树 0 命中（route/multicast_policy.hpp 里是别的类型名）
  三条 policy 所在命名空间  ghostlock::backend::cve_2026_43499::route（multicast_policy.hpp:21）

  => 第 6 条（do_one_write）的有效候选只剩三条：
       ghostlock::attack::do_one_write(...)   旧树
       ghostlock::ops::do_one_write(...)      旧树
       do_one_write(...) 裸名                 新树 main.cpp 里已有
     其余二十来个是历史拼写，resolve 不会命中。

  第 1-3 条（*_thread）与第 4-5 条（run_main_route_threads / do_kernel5_fake_lock_route）的候选
  是新旧两树都在用的形态，但新树当前用的是 *_main 与无此名 => 见 annex 的 §4/§5。

结论与验收次序：
  符号层约束比先前估计的窄：搬入时保住 do_one_write 与几个 *_thread 名即可。
  但 MISSING 不能直接读成"没搬进去" —— 也可能是"搬了但名字不在候选表里"。
  故搬入后的第一步验收是"符号是否存在"（llvm-objdump 查这 6 个名字），
  命中之后再谈 cmp_disasm 的三档比较。
```

## 9. lib/ 与 fixtures/ 现状（2026-10-12）

```
core/lib/   只剩 lua 1.5M · nlohmann 940K · spdlog 1.5M => mpack 删得干净，无残留
core/fixtures/  wire-43284-{kernelcall,lowlevel,needs-sa,steps}.{json,framed} · wire-43499-collide-A.{json,framed}
  => 每个样例 .json 与 .framed 成对；带帧那份正是 wire_doc.cpp 里 wire-no-frame 检查的对象
  => 不是垃圾，是 wire 的成对验收样例，搬入后脚本输入可直接复用
core/Makefile 30 处 · core/src 7 文件 仍引用 mpack => 删依赖已完成，改引用未做（属 P4 范围）
```
