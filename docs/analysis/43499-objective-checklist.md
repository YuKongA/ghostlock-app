# 43499 · 目标清单逐条核对（round 241）✓

## 核对表（✓，每行都有位置 ✓）

| 目标条文 | 落地位置 | 状态 |
| --- | --- | --- |
| 解析 wire → json + lua | `main` 的 `WireDoc` / `report.doc` + Lua 桥 ✓ | ✓ |
| 启动 lua、可控可调、可读 json | `surface.*` / `cfg[...]`（43499_window.lua 在用 ✓） | ✓ |
| 43284 wire 侧写 **sa** | 三个 43284 夹具均含 `step.required_param = ["sa"]` ✓ | ✓ |
| profile steps 下的 **required_param** 列表 | `step.required_param` ✓ + `check_required_params`（main:1475/1650 ✓） | ✓ |
| 封装好的步骤 w1/w2/w3（43499）与 43284 同步 | 夹具 `procedure.steps` ✓ + step 表 ✓ | ✓ |
| **lua 直调底层 kernelaccess** | `kernelcall` 表面行 ✓（kSurfaceCount=9 ✓） | ✓ |
| pagecache_load_lkm 封装成 script 接口，用 lua 调 | 读法② 收官（本晚前段 ✓） | ✓ |
| pagecache_load_lkm 重写成 lua（不调封装接口） | 同上 ✓ | ✓ |
| **43499 成功获取 root（uid=0）** | —— | **未达成** ✗ |

## 说明（✓）

```
本轮专门回到**目标自身的条文**逐条核 ✓（而不是继续在 select 的仪器上花时间 ✓）；
结论：**除最后一条外全部落地** ✓；而最后一条（43499 拿 root ✗）正是本晚一直在追的那条 ✓。
⇒ `required_param` 这一项的实证：43284 三个夹具都有 `["sa"]` ✓、
   `main` 有 `check_required_params` ✓ 并在启动时调用 ✓ ⇒ 不是"文档写了但没用" ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
