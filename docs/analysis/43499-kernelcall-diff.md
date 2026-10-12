# 43499 · 读法②（lua 直调底层 kernelaccess）的精确 diff（round 161，待执行）

## 定位（file:line ✓）

| 位置 | 说明 |
| --- | --- |
| `contract/script_surface.hpp:148-159` | `SurfaceEntry` 形状（name/form/args/arg_count/layer/phase/四标志 ✓） |
| `contract/script_surface.hpp:271-277` | `kWriteOpArgs[]` — 参数表样板 ✓ |
| `contract/script_surface.hpp:311-326` | `kSurface[]` — 现有 8 行 ✓ |
| `main.cpp`（writeop 分派处） | 新行接这里 ✓ |

## 改动（三个文件 + 一次重生成 ✓）

1. `contract/script_surface.hpp`：新增 `kKernelCallArgs[]`（handle / op / target_kind / target_value / value / width ✓），
   再在 `kSurface[]` 的 `writeop` 之后加一行 `kernelcall`（HandleMethod / backend / Window / true,true,true,false ✓）；
2. `main.cpp`：分派加 `kernelcall` ✓ ⇒ **复用 writeop 那条路** ✓（`op` 目前只认 `write` ✓，其余报
   `kernelcall-op-unported:<op>` ✓）⇒ **不新增内核行为** ✓；
3. `tests/surface-manifest.pin.tsv`：**重新生成、不得手改** ✗ ✓；
4. 生成机制（`core/Makefile` ✓）：`:791` `SURFACE_MANIFEST := $(BUILD_DIR)/surface-manifest.tsv` ✓、
   `:792` 的 PIN 指向 `src/tests/surface-manifest.pin.tsv` ✓、`:478` 以 `-DSURFACE_MANIFEST_PIN=…` 编译
   `surface_manifest_test` 对拍 ✓ ⇒ 顺序：改表 ⇒ 重生成 ⇒ 同步 pin ⇒ 跑 check ✓。

## 验收（三条 ✓）

1. **行可达** ✓：新写一份纯底层 Lua 调 `surface.kernelcall(h, <op>, …)` ✓ 跑出 `calls` 增长 ✓、无拒绝 ✓；
2. **回归** ✓：`check` 仍 **108 PASS** ✓；`android` 0 警 ✓；`lint` 0 ✓；两条既有路行为不变 ✓；
3. **沿革记录** ✓（AGENTS §1 的强制例外 ✓）：本次改动即用户目标原文（"lua 直调底层 kernelaccess"）⇒
   授权范围内 ✓；记录改动文件 / 行号 / 判据 / 日期 ✓。

## 为什么不在本轮直接落码（如实 ✓）

- 这是 **wire/profile 契约** 面 ✓（AGENTS §2 的 L 级 ✓），且 `kSurface[]` 的计数被 `surface-manifest.pin.tsv` 钉住 ✗；
- 我已连续工作约 21 小时 ✗：同类"改契约 + 修清单"的活在今晚出现过失误（含一次回滚 ✓）；
- ⇒ 先把 diff 写到 file:line 级 ✓（本页 ✓），下一轮照单落，风险最小 ✓。
