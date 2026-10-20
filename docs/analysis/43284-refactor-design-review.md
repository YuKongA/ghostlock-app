# design-critic 评审记录：43284 重构设计

- 时间：2026-10-19T23:41Z
- 目标：docs/analysis/43284-refactor-design.md
- sha256：62dc0d1779e09b7b64cce74a52bba90b74866d6a3ff234eeef2a434821d6854b
- HEAD：5479d2c65c09a8ea95c9e19a34359e74523d4750
- 总判：仍需修订，阻断 9 项

## 阻断项

| 编号 | 问题 | 必须修改 |
|---|---|---|
| C1 | 五类符号不互斥，缺主归属/消费者 | 定分类轴；每个符号只一个主归属 |
| C2 | 未钉住 DFRoot vs DirtyFrag 唯一上游；cleanup/patch_ko 语义不同 | 每步标 commit/path/line；显式不采用项 |
| C3 | C++ 仍有 KernelAccess 残留：AP 继承、session_object/access_factory/contract 使用 | 补当前→目标差异表，同批删除消费者 |
| C5 | Kotlin→JNI→C++ 依赖不唯一：NativeRawEsp/JniPageCacheOps/RawEspWriter 多路 | 只留一个 PageCacheAccess 实现；open/close 归属明确 |
| C6 | Stage 只有 run，无 close/abort/超时/取消/finally 契约 | 补生命周期与具名失败/证据 schema |
| C7 | 资源表缺 crash_dump 子进程/pipe/orphan/raw handle/原字节缓冲 | 按 AGENTS §5 做 Rust 式所有权表 |
| C8 | helper/模块/hook 部分写与 cleanup 失败无状态/回滚 | 每阶段 written/partial/valid；cleanup 幂等具名 |
| C9 | 五批迁移不可执行，无文件/命令/回滚/门禁 | 拆小批，每批可验收 |
| C10 | 与 DFRP/KPLAN 活跃计划冲突，未声明取代 | 文档头写取代关系，更新 MASTER/ROADMAP |

## 重要项

C4 同名 AccessParams；C11 API 不完整；C12 路径/namespace/CMake/JNI 符号缺失；C13 payload 来源与 464/472/468 三套长度；C14 cleanup 与 shutdown 顺序；C15 ELF 规划三处；C16 测试矩阵；C17 文档头；C18 实验开关清理。

## 已确认

- 顶层顺序 helper→KO→libc++→trigger→cleanup 与 DFRoot/DirtyFrag 一致；
- raw_esp.cpp 已是无 KernelAccess 的纯库；
- 当前 AccessParams 仍继承 KernelAccess，session_object/access_factory/contract 仍引用。

## 未验证

- 未构建/测试；未证明删除后编译；未做设备门禁；未验证 464/472/468 与 ko_target 注入。

