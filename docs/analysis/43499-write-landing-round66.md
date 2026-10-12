# 43499 写落地 · 缺口全景与本轮侦察结论（2026-10-11 清晨 round 66）

## 已修（本晚，每处都带注释、都可复核）

| # | 缺口 | 现象 | 修法 |
| --- | --- | --- | --- |
| 1 | `shared_.owner_started` **从未置 1** | `requeue()` 死等 ⇒ 一次都没调用过 | owner 创建成功处补 `.store(1)` |
| 2 | 挑页未按**内核侧**奇偶 | `layout_accepts_page` 恒 false ⇒ 拒绝 | `payload_for_zero_write(alias_delta)` + 页选择移到 alias 之后 |
| 3 | 载荷映射只有 2 个 64 KiB 候选 | 同一页可能都不合格 | 独立 1 MiB 映射（16 候选） |
| 4 | requeue 的 **nr_requeue** 走 timeout 参数传 nullptr ⇒ 0 | `ret=0`（只唤醒不搬） | 传 `(timespec*)1` |
| 5 | requeue 的 **val3** 写死 0 | 会被判 EAGAIN 或搬 0 | 新增 `futex_val3`，传目标字此刻的值 |
| 6 | 引擎契约误读 | 我传 `victim_target` ⇒ `status=4/59 attempts=0` | 回退为配置里那一对（写什么由**页面**决定） |

修完后：`TEMPREQ after ret=-1 errno=11`（与归档成功的 `-1/35` 同形 ✓）、`accepts=1` ✓、step 全程 RC=0 ✓；
但**写仍未落地**（`comm after len=15` ✗）⇒ 剩下的缺口见下。

## 本轮侦察出的**最后缺口**（下一轮的工作，已具名）

1. **凭据路径从未接上**：`credential_image.hpp:61 build_credential_image(…)` **全树无人调用** ✗；
   而归档成功那次是 `W2: cred === target=… mode=2 le…` ⇒ `WriteMode::Credential` + 目标 `child_task + 0x798(1944)` ✓；
2. **LayoutInputs 要由文档算**：`layout_from_document.hpp:74` 才是正确来源（含 `credential_value` = init_cred 别名 ✓）；
   我 main 里手搓的三个值是**占位**（`cred_copy` 槽地址 ✗）⇒ `Credential` 模式下 `right` 会是错的 ✗；
   参考实现：`run_pipeline_temp.cpp:889-896` ✓（它在打 `credential_value` ✓）；
3. **页面里 `slots.cred_copy` 没有内容** ✗（页构建里没有任何 `cred_*` 逻辑 ✓）⇒ 需要把 `build_credential_image` 的输出放进去 ✓，
   值来自设备 profile：`usage_value=256`、`caps_value=2199023255551`、`caps_count=3`、`copy_size=176`、`ref0..3_offset=128..152` ✓。

## 下一步（一轮能做完，判据两条都要）

1. main 改用 `layout_from_document(…)` 填 `LayoutInputs`（含凭据别名 ✓）；
2. 用 `build_credential_image` 把镜像写进 `cred_copy` 槽 ✓；
3. `page_request.mode = Credential`、`page_request.target = victim_task + cred_off` ✓；
4. 跑 ⇒ 判据：`victim uid=0`（受害者自报 ✓）；comm 那条探针留作"写是否落地"的旁证 ✓。
