# Kernel Symbol Extractor 计划迁移指针

> 2026-11-18 用户裁定：通用 extractor 改为独立 **Kernel Symbol Extractor** 库，发布到 Maven，再由 GhostLock 固定版本引用。原同仓 module 方案被取代。

计划正文的唯一权威已移动到：

- 当前本地权威：`/Users/nickji/Documents/Program/Project/KernelSymbolExtractor/docs/plan/kernel-symbol-extractor-plan.md`
- 计划中的 repository 路径（尚未 commit／push，当前不能当作已发布入口）：`https://github.com/NickJi2019/KernelSymbolExtractor/blob/main/docs/plan/kernel-symbol-extractor-plan.md`

GhostLock 本仓只保留 Maven consumer、backend/provider request、profile projector 与产品门禁责任；不得从本文件恢复 extractor 源码 module、Capstone CMake include、Git submodule/composite build、checked-in AAR/JAR 或旧 Rust fallback。独立库计划的设计批准、实现、发布与 GhostLock product cutover 仍分别经过各自 gate；本迁移不把任何 `NOT_RUN` 改成 `PASS`。

若GhostLock后续持久化library返回的report/expectation objects，真实性与完整性属于GhostLock自有格式合同：必须先验证自有authenticated envelope／record，再重建Java objects，再调用library semantic verifier，最后进入projector。library verifier不证明caller persistence authenticity，也不能发现对整个dynamic ledger的coherent rewrite。

独立库 K1 已交付 Java 对象合同和内部 Kotlin/JVM 引擎。2026-11-19 首发范围收敛到 `:api/:engine/:android`，桌面 facade 延后。当前构建、签名演练和隔离样例的证据，以及此前四坐标的历史结果，均由独立库权威计划维护。GhostLock 的 Maven consumer 仍按本仓独立批次批准。
