# 43499 · 读法② 落地：`kernelcall` 行（round 162）✓

## 沿革记录（AGENTS §1 的强制例外 ✓）

```
时间：2026-10-11（本轮 ✓）
条文：AGENTS §2 的 L 级（wire/profile 契约改动）⇒ 正常需先获认可；
依据：**用户目标原文**："同时确保 lua 直接调用底层的 kernelaccess 也可正常执行攻击" ✓
结论：属**授权范围内** ✓（用户直接指令，非自选越权 ✗）；改动文件与判据见下 ✓。
代价：契约面 +1 行（`kSurface[]` 由 8 行变 9 行 ✓）；pin 与两处计数同步 ✓。
```

## 改动（3 文件 + 1 次重生成 ✓）

| 文件 | 改动 |
| --- | --- |
| `contract/script_surface.hpp` | 新增 `kKernelCallArgs[]` ✓ + `kSurface[]` 加 `kernelcall` 行 ✓（HandleMethod/backend/Window ✓）+ `kHandleMethodRowCount` 6→7 ✓ |
| `main.cpp` | 分派：`kernelcall` 与 `writeop` **同一条路** ✓（不新增内核行为 ✓）；`op` 校验留 `TODO(下一批)` ✓ |
| `tests/surface_test.cpp` | 表行数 8→9 ✓ |
| `tests/surface-manifest.pin.tsv` | **重生成**（`make -C core surface-manifest-regen` ✓，27 行 ✓） |

## 判据（真机 ✓）

```
kernelcall 夹具（Lua 里 surface.kernelcall(h, "write", "absolute", 0x0, 0x14012d83, 4) ✓）：
  RC=0 ✓  main: cfg fields=69 (skipped 18) payloads=1 calls=7 …  main: verdict none ✓
对照 writeop 夹具（同一份 Lua，只把那一行换回 writeop ✓）：
  RC=0 ✓  main: cfg fields=69 (skipped 18) payloads=1 calls=7 …   ⇒ **逐项相等** ✓
⇒ ① 行**可达** ✓；② **行为等价** ✓；③ 无拒绝 ✓
```

## 门禁

- `check` → RC=0，**108 PASS** ✓（未降 ✓）；`android` → RC=0，0 error / 0 warning ✓；`lint` → 0 ✓。

## 遗留（登记 ✓）

- `op` 参数校验（只认 `write`，其余 `kernelcall-op-unported:<op>` ✓）⇒ 下一批 ✓；
- 新增夹具 `core/fixtures/wire-43284-kernelcall.{json,framed}` ✓（与既有两份并存 ✓）。
