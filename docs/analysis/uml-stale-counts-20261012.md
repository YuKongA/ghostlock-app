# UML 过期项清单（2026-10-12 · 只读核实，未改任何文件）

## 1. 对象与结论

```
文件：docs/development/full-process-uml.md（24418 B · 5 节 · 权威 UML）
结论：其中一节及其六处引用写的是**旧条数**，并有一处派生行数也已脱钩。
      该处不是"一次遗漏"，而是同一处数字被三次改动各漏一次（kernelcall · step · log）。
```

## 2. 实测对照

```
暴露面条数
  kSurface[]（core/src/contract/script_surface.hpp:346-370）实测 10 条：
    backend · writeop · kernelcall · copyop · settle · handoff · step · cfg · close · log
  UML 写 7 条（六处）

manifest 行数
  core/src/tests/surface-manifest.pin.tsv 实测 29 行（2180 B），已含两行 log：
    log  call  backend  stage  false true true  level true 16
    log  call  backend  stage  false true true  key   true 256
  UML §3.1.1 写「19 行逐字节」=> 差 10 行
```

## 3. 九处位置（逐处）

```
:4    依据行（引 lua-surface-design「暴露面 7 条」）—— 引的是历史表述，改否属用户裁定
:125  类图     class SurfaceTable { 唯一权威 · 7 条 + cfg }
:142  关系图内 暴露面 7 条唯一权威
:162  引用线   SurfaceTable --> ScriptSurface : 暴露面 7 条
:192  表格行   | **`script_surface`** | 契约层暴露面（**7 条 + `cfg`**）|
:198  节标题   ### 3.1 暴露面 7 条 → 实现层
:200-208  该节表格只有 7 行 —— 缺 kernelcall / step / log 三行
:212  「19 行逐字节」—— 实测 29 行
:215-232  manifest 片段 —— 需由生成目标重新产出后整段替换
```

## 4. 处置约束（两条不得手改）

```
core/Makefile:817-819
  SURFACE_MANIFEST     := $(BUILD_DIR)/surface-manifest.tsv
  SURFACE_MANIFEST_PIN := src/tests/surface-manifest.pin.tsv
  .PHONY: surface-manifest surface-manifest-regen surface-doc-check
:858  surface-manifest-regen: $(BUILD_DIR)/surface_manifest_test   （依赖测试二进制）
=> :212 的行数与 :215-232 的片段必须由 make -C core surface-manifest（必要时 -regen）产出，
   不得手改：AGENTS 称 manifest 为"派生源"，手改会破坏派生性质。
=> 另：:4 引的是历史设计文档的原始表述，改写它等于改写"依据"，需用户裁定。
=> 另：:200-208 补三行时，实现层/相位/备注要按 kSurface 的字段填，不能照抄 pin（pin 是 tsv，语义字段不同）。
```

## 5. 与其它批次的关系

```
属文档订正，不属「清理代码」（清冗余）也不属 P4（删依赖）：
  它是"权威文档与实现脱钩"的独立项，单列上报。
前置：:212 与 :215-232 需要能构建才能由生成目标产出 => 卡在 mpack 引用面（P4）之后。
```

## 6. 已核非过期的一处（撤回记录，2026-10-12）

```
UML :113 与 :210 写"新树 9 层"，一度怀疑与实现脱钩。实测：

  core/src/contract/layer_scope.hpp:33   kLayers[] 共 9 行：
    contract(Op) · support(Op) · memory(Op) · backend(Op) · pipeline(Window) ·
    terminal(Op) · script(Script) · session(Op) · profile(Op)
  :44  kLayerCount = sizeof(kLayers)/sizeof(kLayers[0])

  => 9 层，与 UML 一致 => 该处非过期项，撤回。本清单范围不变（仍为暴露面条数与 manifest 行数）。
```

## 7. 一条方法论收获：第二处拼写需要交叉校验

```
core/src/contract/contract.cpp:13 注释逐字：
  "The names are a second spelling of kLayers, so they are checked against ..."
=> 本树对"同一事实的第二处拼写"的做法不是禁止，而是要求交叉校验。
   层表如此（kLayers 与 contract.cpp 的名字互校），暴露面表亦如此（kSurfaceCount 与命名检查）。

而 UML 里的数字正是"第二处拼写"，却没有被纳入任何校验 => 它才会悄悄脱钩（10 条写成 7 条、
29 行写成 19 行）。若将来要给 UML 的数字加校验，形态应当照 contract.cpp:13 这一处写。
```
