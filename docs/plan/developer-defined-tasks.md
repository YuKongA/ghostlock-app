# 开发者定义任务清单

> 本文件只接收开发者明确指定的条目；Agent 只能更新既有 checkbox 状态，禁止自行增加、删除、改写条目或加入任何解释。

- [x] Payload 继续使用 `payload-v3.json`。
- [x] Finish 继续使用 `finish-v3.json`。
- [x] Payload 与 Finish 保持独立 authority。
- [x] 取消 Finish 迁移到 SharedPreferences 的方向。
- [x] 新安装默认 Finish 包含 `finish-restore-selinux`。
- [ ] 未检测到 Root Manager 时，默认启动 Root Manager 项显示具名 Incomplete。
- [ ] 删除 Payload 页面底部的伪 Save。
- [ ] Payload 页面右上角增加 Payload-only Preview。
- [ ] Preview 返回纯文本且对 Finish 零读取。
- [ ] 完成代码编辑器依赖 Spike 与最终选型。
- [x] Diagnose backend 在 debug 与 release 构建中均可用。
- [x] Diagnose backend 包含实际 native C++ 实现并编译为 shared library。
- [x] Diagnose Flow 实际编排 native、JNA、shared library、log 与 handoff shell 全链路。
- [ ] `P-UI-ICON-002`：为 Material 和 Miuix 的每一个 icon 添加项目目录项，而不只收录项目当前使用的 icon。
- [ ] 重新审视模糊的 `RunHost*` 命名，并按实际职责合理改名；保留确实表达跨进程执行宿主边界的名称，禁止只做无结构收益的批量 cosmetic rename。
