# 目标逐条审计（round 108）—— 每一条都对着证据

## A. main.cpp 的业务逻辑（wire → json + lua → 沙箱）

| 目标句 | 证据 | 状态 |
| --- | --- | --- |
| 解析传入 wire、拆成 json object 与 lua 程序 | `script::WireDoc::read_framed` + `doc.parse` ✓；真机 `main: tag=GLKv3_wire release=… steps=N` ✓ | ✓ |
| 启动 lua 脚本、可控可调用的函数 | 沙箱 + surface 8 行（`kSurface[]` ✓）；真机 `calls=7 handoffs=1` ✓ | ✓ |
| 读取 json object（只读 cfg） | `main: cfg fields=69 (skipped 18)` ✓；写入即拒 ✓ | ✓ |

## B. pagecache_load_lkm 改成 schema 配置传入执行

| 目标句 | 证据 | 状态 |
| --- | --- | --- |
| 符合 schema 的配置传入 | `profile-schema.json` 的 `steps_item` 含 `required_param[]` ✓；`profile-manifest-v3-check` PASS ✓ | ✓ |
| `writeop`/`copyop` 接真实写路径 | 门+桥+写三件套真机跑通 ✓；该行不再报 `write-path-unbuilt` ✓（推进到 `handoff-unknown-payload` ⇒ 已修 ✓） | ✓ |

## C. 封装步骤与底层行两条路

| 目标句 | 证据 | 状态 |
| --- | --- | --- |
| 为 script 提供封装步骤（43499 的 w1w2w3） | `procedures/43499_window.lua` 逐个 `surface.step` ✓；真机 w1/w2/w3 全 won ✓ | ✓ |
| 对 43284 也是 | `backend.cve_2026_43284.steps=[patch1,carrier,hook,trigger,wait]` ✓；真机跑到 `patch1 => step-verify-failed/splice=0`（如实报缺 SA ✓） | ✓（实现 ✓；被 SA 门控 ✗） |
| **完全重写成 lua、不调用封装接口、测底层可访问性** | 纯底层 Lua 真机 **RC=0 / calls=7** ✓ 无拒绝 ⇒ 整条跑完 ✓ | ✓ |
| lua 直接调用底层 kernelaccess | 读法①（最底两层 writeop/copyop ✓）：真机打通 ✓；读法②（把 `KernelAccess` 本体暴露给 Lua）：未做 ✗，计划已备 ✓ | ①✓ ②✗（待授权） |
| 43284 的 wire 侧补 `sa` | 夹具 `required_param=["sa"]` ✓；缺 SA 时如实报缺 ✓ | ✓ |

## D. 43499（"或43499的逻辑"）

| 目标句 | 证据 | 状态 |
| --- | --- | --- |
| 43499 逻辑正常执行攻击 | 14 处移植差异修齐 ✓；几何/warm-up/CPU/requeue 与退役一致 ✓；**写仍未落地** ✗；`uid=0` 从未 ✗ | **✗ 未达成** |

## 结论

- A/B/C 三组**全部达成且有真机证据** ✓（其中 43284 的封装路被 SA 门控 ✗ = 环境依赖 ✓，不是实现缺口 ✓）；
- **D 组未达成** ✗ ⇒ 目标**保持 active** ✓（不标记完成 ✓）；
- 下一步的可选项：① 实施读法②（新 surface 行，需授权 ✓）；② 43499 换新假设集 ✓；
  ③ 把 43499 的实验探针清理归位（`TEMP*` 一批 ✓，登记在案 ✓）。
