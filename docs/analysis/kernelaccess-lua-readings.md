# 43284/43499 · "lua 直接调用底层 kernelaccess" 的两种读法与证据（round 107）

## 事实（读码 + 真机）

```
contract/script_surface.hpp:311  kSurface[8] = backend / writeop / copyop / settle / handoff / step / cfg / close
                                  （注释原文：the surface table … 8 rows ✓，且 no window.* row may appear ✓）
contract/kernel_access.hpp:48     class KernelAccess（+ KernelAccess::create(BackendId) ✓）
                                  ⇒ 这是**引擎侧 C++ 接口**，**不是** Lua 的一行 ✓（有意如此 ✓）
真机（纯底层 Lua）：RC=0  calls=7 handoffs=1 steps=0 ✓ ⇒ 8 行里被脚本用到的全部打通 ✓
```

## 两种读法

| 读法 | 判据 | 现状 |
| --- | --- | --- |
| ① "底层行"= surface 的最底两行（writeop/copyop ✓） | 纯底层 Lua 整条跑完 ✓ | **已达成** ✓（RC=0 / calls=7 ✓） |
| ② 把 `KernelAccess` 本体暴露给 Lua（新增 surface 行 ✗） | 新增一行的行为与判据 | 未做 ✗ ⇒ **属攻击路径改动 ⇒ 先计划** ✓ |

## 如果要做读法 ②（计划要点，未实施 ✓）

1. `kSurface` 增一行（例如 `kernelcall`），`SurfaceForm::HandleMethod` ✓、相位 `Window` ✓；
2. 参数表沿用既有风格（kind + 数字，值不携带内核偏移 ✓）；
3. 分发到 `KernelAccess` 的现有方法（**不新增内核行为** ✓）；
4. 判据：① 该行在真机上被脚本打到 ✓；② 封装步骤与既有两半**逐字不变**（回归 ✓）；
5. 按 AGENTS §2：先出计划、获认可后再写码 ✓。
