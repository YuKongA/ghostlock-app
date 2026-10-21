# 43284 行级攻击审查记录

- 日期：2026-10-21
- 快照：HEAD `92d55d3e`，工作树 dirty 158 项
- 对比：DFRoot `/tmp/DFRoot/app/src/main/jni/exp.c`；旧临时接线 `5479d2c6`
- 事实：emulator 当前 Stage smoke 7/7、restore ok、dfm6 hit；A301SO 5.15.189 android13 SELinux enforcing 仍约 2s panic，未到 finish/cleanup/manager

## 已确认（S1–S5，保留）

| 编号 | 严重度 | 差异 | 影响 |
|---|---|---|---|
| S1 | 阻断 | 旧版先全量 verify payload 再写 site；当前 HookStage 先写 site，之后只验 site+payload 首/尾 | payload 中间块缺失时 site 已生效，init 跳入空洞 → 2s panic |
| S2 | 阻断 | 旧版有 `moduleAlreadyPresent` 全量跳过；当前每次重写 module | carrier 已被写过时，当前重写 2544 附近 → panic |
| S3 | 阻断 | 旧 native `writeImage` 逐块 verify/retry；当前只记 written | 中间块未落地仍继续 hook/trigger |
| S4 | 阻断 | 旧 payload mutex `/dev/dz`；当前仍 `/dev/df`，仅脚本末尾删除 | DFRoot/旧版残留 `/dev/df` 时 payload O_EXCL 失败，site 命中也不执行 |
| S5 | 重要 | 旧版 trigger 前二次 apply site；当前只写一次 | site 页淘汰后 init 不执行 payload |

## 新增细节（N1–N8）

| 编号 | 严重度 | 细节 | 影响 |
|---|---|---|---|
| N1 | 重要 | RestoreLedger 只记录 helper/stage/site，未记录 module carrier 原字节 | cleanup 不撤销 module 写入；下一轮可能使用坏 carrier |
| N2 | 重要 | 每个 RestoreLedger entry 只验首 16B | stage 464B/helper 2288B 中间块恢复失败仍判 ok |
| N3 | 重要 | WaitStage 失败仍等 10s 才 cleanup | site 保持活跃 10s，init 持续命中，可能 panic |
| N4 | 重要 | module/hook verify 只首/尾块 | 中间块丢失可绕过，site 跳坏 landing |
| N5 | 重要 | payload 仍 `/dev/df`，无旧版 `/dev/dz` 替换 | 残留 mutex → payload 不执行 |
| N6 | 重要 | 当前 LKM 无旧版 defex，也无 DFRoot AVC/Oppo/init 域过渡 | 更可能 no finish/manager；旧 LKM 无 L1/L2 也能跑，非首因 |
| N7 | 重要 | 只写一次 site，无 trigger 前 reapply | 页淘汰时 site 不执行 |
| N8 | 建议 | 无 `moduleAlreadyPresent` | S2 的延续 |

## 其他

- `KotlinSentryPlan.displacedOffset=0x1D0` / `jumpBackOffset=0x1D4` 与 `DfrootLibcxx.FIRST_INST_OFFSET=0x1C8` / `JUMP_BACK_OFFSET=0x1CC` 常量不一致；当前实际按 DfrootLibcxx 写，暂非主因，需统一。
- 实际 HEAD 是 `92d55d3e`，不是 `0ed4e6d6`；后续快照需重新冻结。


## 追加细节（N9–N12）

| 编号 | 严重度 | 细节 | 影响 |
|---|---|---|---|
| N9 | 重要 | module 原字节没有进 RestoreLedger；cleanup 只恢复 helper/stage/site | ModuleStage 写坏 carrier 后无法撤销，下一轮/下一阶段继续使用坏 module |
| N10 | 重要 | module verify 只读首/尾；中间块失败仍可通过 | 首尾正确、中段缺失的模块仍可能进入 Hook/Trigger |
| N11 | 重要 | RestoreLedger 恢复 helper 时 module/UMH 可能仍在运行 | site/stage 已恢复、module 仍在执行，形成半恢复状态 |
| N12 | 重要 | FinishScript STATUS 未覆盖 launchOnly、FolkPatch APD、kernelAccess 超时、restoreSelinux/soft reboot 失败 | 这些失败仍可能 touch /dev/dfm6，掩盖真实终态 |

## 追加细节（N13–N15）

| 编号 | 严重度 | 细节 | 影响 |
|---|---|---|---|
| N13 | 重要 | FinishScript 顶部 `exec 3>>"$LOG"` 没有失败处理；LOG 打不开时脚本继续执行但所有 log 丢失，也不写 dfme2 | 真机 SELinux/UMH 无法打开 app fd 时，表现为“无 finish、无失败 marker”，等待 10s 后 cleanup |
| N14 | 重要 | CommandPayload 把任意用户命令直接拼成 `cmd || { STATUS=1; ...; }`；命令以 `&`、`;`、管道或 `case` 结尾时可能改变语义或产生语法错误 | 自定义命令失败可能不置 STATUS，或脚本在执行前语法失败，dfm6/dfme2 都不出现 |
| N15 | 重要 | KotlinSentryPlan 的 displacedOffset=0x1D0 / jumpBackOffset=0x1D4 与 DfrootLibcxx 的 FIRST_INST_OFFSET=0x1C8 / JUMP_BACK_OFFSET=0x1CC 不一致；当前 StageConfigProvider 实际按 DfrootLibcxx 写，但 plan 字段仍可能被其他路径使用 | 后续若有人改用 plan 字段写 payload，会写错 4B 偏移，site 跳转/回跳损坏 |

## 当前因果排序

1. S1 + S2/N8 + N4：site 先写后验 + module 重写 + 首尾校验不足
2. S4/N5：/dev/df 残留 → site 不执行
3. S5/N7：无二次 site apply
4. N1/N2/N3：cleanup 不完整/过晚
5. N6：LKM AVC/Defex/init 域；旧 LKM 无 L1/L2 也能工作，暂非首因

## 验证/未验证

已验证：DFRoot、旧 5479d2c6、当前 Stage/helper/raw_esp/LKM 的行级阅读；emulator 7/7 为用户报告，未独立复核。

未验证：A301SO panic 的 dmesg/pstore、panic 栈；S1–S5/N1–N8 的单变量 A/B；旧/新 LKM 2×2；旧版真机 carrier 与 /dev/df 状态。

