# 完备日志点设计 · 43499 全链路（2026-10-12，用户裁定）

## 0. 裁定与现状（逐字）

> 「你这日志也没打印几行啊，给我出完备的日志点设计，叫design-critic来审查完备性」

现状（真机实测）：**8 个点**（4 入口 ＋ 2 分支 ＋ 2 结果），日志 **704 行** —— 但**远不完备**：
上层（wire/prepare/victim/sandbox/terminal）**一个点都没有**，而本会话**大半时间**耗在那里。

## 1. 完备性判据（先定判据，再列点）

    C1 **每个"决策/分支/拒绝"都有一条**：任何 if/else、switch、return <具名串> 处，进入时先说"进了哪条、为什么"，不许只打印结果。
    C2 **每个跨边界两侧各一条**：进程(fork) / 线程(pthread) / 沙箱(Lua) / wire(grating) / 设备(adb)。
    C3 **每个判据数字进日志**：判据用到的每个数（ret/errno/uid/地址/计数）都在日志里，一条 grep 即可判，无需回看 printf。
    C4 **每条日志可唯一对齐**：时间戳到毫秒 + 步骤名 +（多线程处）线程标识。
    C5 **拒绝必须具名**：与代码里的具名串**逐字相同**，日志与代码可互查。
    C6 **级别语义固定**：debug=逐轮/逐次，info=阶段边界，warn=可恢复异常，error=具名拒绝与退出。

## 2. 全链路日志点清单（按执行顺序）

### A. 启动与 wire（main.cpp）

    A1 进程入口：argv / uid / 域(SELinux) / 内核 uname                     [info]
    A2 读帧：长度字段 / body 字节数 / read_framed 结果                     [info]
    A3 解析：schema / release / backend / step.key / route                 [info]
    A4 预算：四预算各自的值（缺失时具名 budget-unset）                     [warn/error]
    A5 必需参数：required_param 逐项（缺则 route-config-missing:<name>）   [error]
    A6 日志启动：级别 / 路径 / 成功或具名拒绝                              [info]
    A7 Lua 装载：code 长度 / base64 解码字节数 / 拒绝时 step-malformed     [info]

### B. 43499 发现与准备（snitch / prepare）

    B1 snitch setup：collisions / threads / timeout                        [info]
    B2 每轮：round / probes / candidates / found 地址                      [debug 逐轮]
    B3 发现判决：verdict / found / mm_struct（规范对象）                   [info]
    B4 prepare 每轮：attempt / base / delta / cand / parity ok             [debug 逐轮]
    B5 prepare 拒绝：具名串逐字                                            [error]
    B6 页构建：slots（waiter/lock/fake_task/fake_lock/cred_copy）/ 页地址  [info]
    B7 凭据镜像：build_credential_image 的具名结果                         [info]
    B8 victim：spawn 成功/失败 / child pid / task 地址 / cred 目标          [info]
    B9 uid 通道：每轮读回的 uid（含失败/超时）                             [debug 逐轮]
    B10 页采纳：layout matches / accepts / pager 判据的两半                [info]

### C. route 与门面（assembly）

    C1 route config：全部数值（与 main: rv 同集）+ 每个缺失键具名          [info]
    C2 route 构造：route 名 / Ops / limits                                  [info]
    C3 access 绑定：成功则 route / landing=1；失败则具名原因逐字               [info/error]
    C4 stage context：chain / target / payload_target / payload_value       [info]
    C5 每次 attempt：轮次 / 走 landing 还是回退                                [debug 逐次]
    C6 route 内部（每分支）：waiter（进入/lock/ready/waiting/park 前/park 结果）
       owner（进入/lock target/started/chain/chain_done）
       trigger（进入/每轮/stamp 结果/warm-up 分支/armed 分支/consumer 反馈/结束码）
       requeue（进入/门控通过/等待者位/结果）
       judge（三分支各一条）/ consumer（每轮/policy/calls/success）
    C7 每轮 end：clean / violations 四计数 / attempts/calls/successes        [info]

### D. 验证与重试（main.cpp）

    D1 写后验证：读回的六个字 + uid 读回           [info]
    D2 重试决策：为何重试 / 轮次 x/y               [info]
    D3 步尾：step result（won/具名拒绝）                                     [info]
    D4 退出：**进程真实退出码** / 首个失败点      [error]（2026-10-12：`rc=9` ⇒ `value=9`；**须为末行**＝drain 判据；旧「Lua 判定 0/10」**已取代**；经 `finish(code)`。）

### E. 沙箱与 Lua（sandbox）

    E1 沙箱开：预算 / cfg 快照字段数 / 拒绝具名                              [info]
    E2 每个 surface 动词调用：名字 / 实参摘要 / 返回                         [debug 逐次]
    E3 surface 拒绝：surface-unknown-name / plan-syntax / handle-stale       [error]
    E4 Lua 结局：script_rejection_name                                       [info]
    E5 handoff/terminal：路径 / 结果 / 拒绝具名                              [info]

### F. Lua 可自选点（暴露）

    F1 surface.log(level, key)：脚本可在每个自选步骤前打点
    F2 建议：procedures/*.lua 在每次 surface.step 前打点

## 3. 为满足 C3 需要的接缝能力（现状不够）

    现状：log(level, what) / log(level, what, value) / log(level, what, a, b)
          / log(level, what, a, b, c, d) / log(level, what, Text{...}) / log(level, what, Hex{...})
    规则：值数增加一律**加重载**（同一个名字 log），**绝不另造函数名** ——
          要选名字的调用者就会漏数。

## 4. 验收

    覆盖：§2 的 A/B/C/D/E/F 各段逐条有落点；§1 的 C1 与 C3 的每一分支都有。
    三线门禁：check 109 PASS 起只增不减 / android 零告警 / lint 0（`--rerun-tasks`）。
    真机：日志行数只作参考；**判据改为逐键覆盖**（见 §7），且判据只认 native 行（见 §8）。
    归档：docs/analysis/device-gates/*.md（单份 <= 8 KB）。

    失败模式（证伪）：删掉 §6 表任一点位（**或 PI 后打印的等价行**；环已撤除）后重跑真机 ⇒ 对应键计数 0 ⇒ 判据红。
    参考指标（不判定）：日志总行数（实测 3464 行）。

## 5. L3 整改：C1 由无界量词改为**可判定清单**（2026-10-12）

```
**问题（critic L3 逐字）**：「『任何 if/else/switch』是**无界量词**（含一切错误检查与支撑代码），
  **无法证明完备**；§4 的验收只对**本文档自身**（不触代码），与『判据须能失败』相抵」

**整改**：C1 之「完备」改由**本清单**判定，清单即**判据**：
  ① 下表每行**必须真实存在**（由 grep 校验）；
  ② 新增点位**必须**登记入表；
  ③ **删一个点则表与代码不符，判据红**（可失败，合 §8）。

## 6. 现存点位清单（file:line，实测 18 处）

- **整表已迁出**（配额）⇒ 见 **[43499-logging-points-annex-points.md](43499-logging-points-annex-points.md)**（18 处点位 + 覆盖分组）。

## 7. L4 与 L11 整改：覆盖式验收（判据，可失败）

**问题**：L11 逐字「『至少 2000 行』**量不等于覆盖**；『一条 grep 定案』**未给命令**」⇒ 不可失败。

**整改**：行数**降为参考指标**；判据改为**逐键覆盖**：对 §6 表里每一条的键，各数一次，各自至少 1；
窗口内 7 类行同样各至少 1。**先剔除**带来源标记的行再计数（见 §8）。

**可失败（证伪）**：删掉 §6 表里任一点位（或它的 POD 环等价）后重跑真机 ⇒ 对应键计数为 0 ⇒ 红。
窗口外的点可在**副本**上删；窗口内的点必须**用脚本**删并复核无残留。

## 8. 判据的过滤规则：只认 native 行

- **可执行形式（命令 · 码表 · 自检与真机输出）已迁出** ⇒ 见 **[43499-logging-points-annex-judging.md](43499-logging-points-annex-judging.md)**（配额纪律：单份 ≤8 KB）。
- **返回值语义**：`0` 通过 · **`2` 失败**（标记行混入判据计数）· `1` 用法错误。

