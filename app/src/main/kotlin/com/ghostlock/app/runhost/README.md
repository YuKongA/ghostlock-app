# RunHost framework owner

`runhost/` 只保留 Android framework 直接构造的 App `:flowhost` 入口 `AppRunHostService`。它不是 backend implementation package，也不拥有第二份 backend membership 或 wire 合同。

当前 owner 划分：

- `core/runhost/`：platform-neutral protocol、request/snapshot model 与 one-shot state machine；
- `data/runhost/client/`：App/Shizuku Binder client、snapshot poller 与 callback log connector；
- `data/runhost/wire/`：generated AIDL Parcelable 与 core model 之间的唯一 codec/Parcel budget owner；
- `data/runhost/host/`：Android workspace/staging 之上的 execution-domain 检查与 product run 准备；
- `data/log/`：与 backend 无关的 FD 日志 connector；
- `runhost/AppRunHostService`：App process-domain framework entry；
- `shizuku/GhostlockUserService`：Shizuku shell process-domain framework entry。

两个 framework entry 复用同一 `app/src/main/aidl/com/ghostlock/app/run/` 合同。它们只能在 framework 边界调用 `BackendRegistry.runtime(key)` 取得 backend-owned runtime projection，不得读 UI catalog、definition list 或建第二份 registry。

App/Shizuku host orchestration 去重是后续独立批次；本目录不改 AIDL method/field/integer、lifecycle 转移、Payload/Finish、JNA ABI 或产品成功语义。
