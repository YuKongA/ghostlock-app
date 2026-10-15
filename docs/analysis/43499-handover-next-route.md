# 43499 · 交接页：canonical 已见底，下一步是 `select_stack`（round 166）

## 一、已达成（都有真机/门禁证据 ✓）

| 项 | 证据 |
| --- | --- |
| wire → json + lua ⇒ 沙箱 | 真机 `main: tag=GLKv3_wire …` ✓ |
| 可控可调用函数 + 只读 cfg | surface 9 行（含 `kernelcall` ✓）；`cfg fields=69` ✓ |
| pagecache_load_lkm → schema 配置 | `steps_item` 含 `required_param[]` ✓；`profile-manifest-v3-check` PASS ✓ |
| 封装步骤（43499 w1w2w3 / 43284 五步） | 两条路都跑到 ✓（43284 封装路如实报 `splice=0` ✗ = SA 门控 ✓） |
| 纯底层 Lua（不调封装接口） | `RC=0` ✓ `calls=7` ✓ `verdict none` ✓ 与 `writeop` 版逐项相等 ✓ |
| read②：lua 直调底层 kernelaccess | `kernelcall` 行 ✓（9 行 ✓ pin 已重生成 ✓）+ 正/负双例 ✓ |
| canonical 链结构 | 14 处移植差异 ✓、四值与归档**逐字相等** ✓、三步三请求 ✓、修复轮 `0/0` ✓、stamp 元组 ✓、re-poison ✓、字节级比对通过 ✓ |
| 门禁 | `check` **108 PASS** ✓、`android` 0 警 ✓、`lint` 0 ✓ |

## 二、未达成（唯一一项 ✓）

```
**43499 写入未发生** ✗：≥9 次完整跑，直读仪器 `selinux enforce` **0 次翻转** ✗、`uid=2000` ✗
判据已备：① 机制级 enforce 1→0 ✓（已体检 ✓）；② 目标级 uid=0 ✓
```

## 三、下一步（按计划 ✓，物料齐 ✓）

```
计划   docs/analysis/43499-select-stack-port-plan.md（5 项 ✓）
研究   docs/analysis/43499-select-stack-study.md（机制四条 + 结构地图 ✓）
快照   docs/analysis/cve-2026-43499-select-stack-route.cpp（640 行 ✓）
夹具   core/fixtures/wire-43499-select-stack.{json,framed} ✓
锚     `main: refused (route-not-ported:select_stack)` ✓（移植前 ✓，5 跑中 2 次 ✓）
```

## 四、执行顺序（每步可单独验 ✓）

1. **本树侧新增 route 本体**（`landing/select_stack_route.hpp` + `…_linux.hpp` ✓）⇒ 只加文件、**不接线** ✗
   ⇒ 判据：`android` 编译通过 ✓（0 警 ✓），既有两条路行为不变 ✓；
2. **接线选择器**（`main.cpp` ✓，`available.cve_2026_43499.route == "select_stack"` ✓）⇒ 判据：夹具跑出
   **本 route 自己的报告** ✓（不再是 `route-not-ported` ✓）；
3. **三级判据**（与 canonical 同一套 ✓）：奇偶/窗口 ✓ ⇒ `enforce` 1→0 ✓ ⇒ `uid=0` ✓；
4. 归档：`device-gates/` 一份 ≤8 KB ✓ + `check`/`android`/`lint` 全绿 ✓。

## 五、交接注意（今晚实测得到的经验 ✓）

- 奇偶校验**先于**路由选择 ✗ ⇒ 判据须**多次统计**，不能只看一次 ✓；
- 契约面改动（surface 行）要同步 **pin**（`make -C core surface-manifest-regen` ✓）与**两处计数** ✓；
- 同批门禁连续失败 2 次即停、回 debug ✓；
- 本树两份自查：**"看起来像差异"必须逐条验证** ✓（今晚两次误判都是这样被纠正的 ✓）。
