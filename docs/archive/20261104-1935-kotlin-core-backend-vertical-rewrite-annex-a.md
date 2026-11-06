# 归档说明

- 原路径：`docs/plan/kotlin-core-backend-vertical-rewrite-annex-a.md`
- 归档日期：2026-11-04 19:35:20 -0500
- 归档原因：所属 KBR 主计划已因错误的 Payload / Finish executor 模型被用户裁定失效；本附件的 write/delete set 不能再作为实施边界。
- 来源：2026-11-04 未提交工作树草案；正文保留为失效计划的附件沿革。现行需求入口为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Kotlin core / backend 垂直整理计划 — 附件 A：exact file ledger

> 状态：KBR0设计附件；production implementation仍未授权。
>
> 历史 authority：[`20261104-1935-kotlin-core-backend-vertical-rewrite-plan.md`](20261104-1935-kotlin-core-backend-vertical-rewrite-plan.md)。该主计划现已失效，本附件不再授权 write/delete。
>
> 基线：`dev-nocpp` / `21d9f53c00ee76891a993c8a2ecea1ea6f9be830` / 2026-11-04。

## A0. 执行规则

1. 下列literal path才是KBR1/KBR2写边界；发现新caller先停手、amend本附件并重过design-critic。
2. action只有`ADD`、`MODIFY`、`MOVE`、`DELETE`、`KEEP_NONCOMPAT_REASON`。`MOVE`目标同时承接KDoc与行为；若语义改变，目标还必须按主计划重新验证。
3. `KEEP_NONCOMPAT_REASON`表示文件留在树中但不是旧backend兼容门，也不授权在KBR顺手改动。
4. KBR1结束即要求旧production symbol/definition/caller为0；KBR2只删除已经compile-time unreachable的island，不首次接runtime线。

## A1. KBR1 ADD

### A1.1 neutral domain 与plugin合同

- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendAvailability.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendConfigurationSpec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendExecutionSpec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendParameterRequirements.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendSpec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/backend/ImmutableBytes.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendPlugin.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendRunPlan.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendFlowInput.kt`

### A1.2 run、wire、observability、Payload、Finish

- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/AttachedFrameSet.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/RunHostProtocol.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/RunHostStateMachine.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/RunVerification.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/HostProcessRunner.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/observability/EventContract.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/observability/EventDispatcher.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/observability/EventFormatter.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/observability/AndroidLogcatEventSink.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/observability/ProcessFileEventSink.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/observability/ProcessLogController.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/observability/RunCallbackEventSink.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadDefinition.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadRepository.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/payload/SharedPreferencesPayloadRepository.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/payload/AndroidPayloadArtifactStager.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadBundleArtifact.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadItemArtifact.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadExecutor.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadVerifier.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadScriptRenderer.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishDefinition.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishRepository.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/finish/SharedPreferencesFinishRepository.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/finish/FinishScriptArtifact.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/finish/FinishScriptRenderer.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/finish/FinishExecutor.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/domain/postroot/PostRootBinding.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootPlan.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootExecutor.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootResult.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootReceipt.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootReceiptAuthority.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/RootExecutionContext.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/RootOperation.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/run/BackendFrameView.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/core/postroot/PostRootFrameView.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/data/migration/LegacyFinishPayloadMigration.kt`：一次读取旧`FinishConfig` blob，以同一Editor/commit原子写出Payload/Finish两个snapshot；迁移失败两边均不发布。

### A1.3 Android transport 与AIDL

- `ADD app/src/main/aidl/com/ghostlock/app/run/IRunHost.aidl`
- `ADD app/src/main/aidl/com/ghostlock/app/run/IRunHostCallback.aidl`
- `ADD app/src/main/aidl/com/ghostlock/app/run/RunHostReply.aidl`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostReply.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/RunWireV3Codec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/ProfileWireV3Codec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/PayloadWireV3Codec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/FinishWireV3Codec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/RunFrameManifestV3Codec.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostProcessContainer.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostCallbackSink.kt`
- `ADD app/src/main/kotlin/com/ghostlock/app/process/AppProcessRole.kt`

### A1.4 greenfield合同tests

- `ADD app/src/test/kotlin/com/ghostlock/app/backend/registry/BackendRegistryV3Test.kt`：`KBR-RG-01..05`。
- `ADD app/src/test/kotlin/com/ghostlock/app/backend/declarations/BackendRunPlanV3Test.kt`：`KBR-PL-01..06`。
- `ADD app/src/test/kotlin/com/ghostlock/app/backend/transport/RunWireV3Test.kt`：`KBR-WR-01..12`。
- `ADD app/src/test/kotlin/com/ghostlock/app/backend/transport/RunFrameTransferV3Test.kt`：`KBR-FR-01..12`。
- `ADD app/src/test/kotlin/com/ghostlock/app/core/run/RunHostStateMachineV3Test.kt`：`KBR-ST-01..12`。
- `ADD app/src/test/kotlin/com/ghostlock/app/backend/transport/RunHostAdapterParityV3Test.kt`：`KBR-AD-01..04`。
- `ADD app/src/test/kotlin/com/ghostlock/app/core/observability/EventContractV3Test.kt`：`KBR-EV-01..06`。
- `ADD app/src/test/kotlin/com/ghostlock/app/core/finish/PayloadFinishBoundaryV3Test.kt`：`KBR-PF-01..08`。
- `ADD app/src/test/kotlin/com/ghostlock/app/architecture/KbrBoundaryV3Test.kt`：`KBR-AR-01..10`。
- `ADD app/src/androidTest/kotlin/com/ghostlock/app/backend/transport/DebugProbeRunHostContractTest.kt`：真实Binder Stub/Proxy覆盖prepare、attach、start、snapshot、destroy与typed reply。

## A2. KBR1 MOVE并改成新合同

- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/registry/Backends.kt -> app/src/main/kotlin/com/ghostlock/app/backend/registry/BackendRegistry.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/host/FlowHostService.kt -> app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostService.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/launcher/AppProcessFlowLauncher.kt -> app/src/main/kotlin/com/ghostlock/app/backend/transport/AppRunHostClient.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/launcher/ShizukuProcessFlowLauncher.kt -> app/src/main/kotlin/com/ghostlock/app/backend/transport/ShizukuRunHostClient.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/launcher/RunCoordinator.kt -> app/src/main/kotlin/com/ghostlock/app/core/run/RunController.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/backend/backends/debugprobe/FlowDebugProbeEntry.kt -> app/src/main/kotlin/com/ghostlock/app/backend/backends/debugprobe/DebugProbeFlow.kt`：仅debug诊断Flow；不改名为Program。
- `MOVE app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishEditor.kt -> app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadEditor.kt`：root manager、safe mode与payload item全部归Payload domain。
- `MOVE app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishEditorPort.kt -> app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadEditorPort.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/domain/finish/ReadFinishEditorUseCase.kt -> app/src/main/kotlin/com/ghostlock/app/domain/payload/ReadPayloadEditorUseCase.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/domain/finish/SaveFinishEditorUseCase.kt -> app/src/main/kotlin/com/ghostlock/app/domain/payload/SavePayloadEditorUseCase.kt`
- `MOVE app/src/main/kotlin/com/ghostlock/app/data/finish/FinishEditorAdapter.kt -> app/src/main/kotlin/com/ghostlock/app/data/payload/PayloadEditorAdapter.kt`

## A3. KBR1 MODIFY

### A3.1 composition、manifest与build

- `MODIFY app/src/main/kotlin/com/ghostlock/app/AppContainer.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/AppViewModelFactory.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/GhostLockApp.kt`
- `MODIFY app/src/main/AndroidManifest.xml`
- `MODIFY app/build.gradle.kts`：AIDL source、新tests与旧source移除；不保留旧transaction配置。
- `MODIFY build.gradle.kts`：删除固定test-count authority；只保留XML failure/error/skipped检查。
- `KEEP_NONCOMPAT_REASON settings.gradle.kts`：KBR仍是现有`:app`，本批不拆module、不改project graph。

### A3.2 backend plugins与backend-private callers

- `MODIFY app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/BackendCve43284Glued.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/BackendCve43499Glued.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/backend/backends/debugprobe/BackendDebugProbe.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/BackendCustom.kt`：静态`CUSTOM` plugin，保持`Unverified`；移除dynamic registry行为但保留Lua Flow产品入口。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/FlowLuaImpl.kt`：冻结旧Lua Flow字节；静态CUSTOM仍Unverified，后续Lua/JNI bring-up再接EventEmitter与operation catalog。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/SequentialFlow.kt`：冻结现有attack step/retry/cleanup/global-log行为；新live DebugProbe不复用该实现。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimNativeJni.kt`：本批不移动JNI owner；后续backend bring-up再按设备门处理。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimNative.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimSession.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimScriptLauncher.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimCapability.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimIdentity.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimProtocol.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/victim/VictimResult.kt`

### A3.3 domain、data与application run

- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendCatalogPort.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendConfiguration.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRegistryIdentity.kt`：同APK registry不传identity/digest。
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRegistrySnapshot.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRuntimeSpec.kt`：catalog snapshot直接承载neutral `BackendSpec`，不保留第二spec DTO。
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendSelectionPort.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/GetBackendCatalogUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/backend/SelectBackendUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/run/RunBackendUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/run/RunCommand.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/run/RunSessionPort.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/run/RunSessionState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/AndroidBackendConfigurationAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/BackendCatalogAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/BackendPresentationAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/BackendRunSessionAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSelectionAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSessionStore.kt`

### A3.4 finish、observability与settings callers

- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishSettings.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishSettingsPort.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/finish/PreviewFinishScriptUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/finish/ReadFinishSettingsUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/finish/SaveFinishSettingsUseCase.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/diagnostics/AndroidDiagnosticReportBuilder.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/data/settings/SharedPreferencesOperationalPreferencesAdapter.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferences.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferencesPort.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferencesUseCases.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/log/NativeLogBridge.kt`：只接新run/process sink；native attach/close ABI不变。

### A3.5 Shizuku与UI callers

- `MODIFY app/src/main/kotlin/com/ghostlock/app/shizuku/GhostlockUserService.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendChoiceDocumentConfigCard.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendConfigRenderer.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendDocumentConfigCard.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendStatusTone.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendToggleConfigCard.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/component/backend/DefaultBackendConfigCard.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/component/statuscard/BackendStatusCard.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/home/HomeMaterial.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/home/HomeMiuix.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/home/HomeScreen.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/home/HomeUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/BackendPage.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/BackendPageUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishEditorUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishSettingsUi.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishSettingsUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishUi.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/run/RunActions.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/run/RunPage.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/run/RunUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/settings/SettingsMaterial.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/settings/SettingsMiuix.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/settings/SettingsScreen.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/screen/settings/SettingsUiState.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/FinishEditorViewModel.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/FinishSettingsViewModel.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/HomeViewModel.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/RunViewModel.kt`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/SettingsViewModel.kt`

## A4. KBR1 DELETE

### A4.1 old AIDL、declaration、host与launcher

- `DELETE app/src/main/aidl/com/ghostlock/app/flow/IFlowHost.aidl`
- `DELETE app/src/main/aidl/com/ghostlock/app/flow/IFlowHostCallback.aidl`
- `DELETE app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockCallback.aidl`
- `DELETE app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockUserService.aidl`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendConfig.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendConfigurationSpec.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendExecution.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendFlowParams.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendParameterSchema.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendReadiness.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendRegistryIdentityFactory.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendSpec.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/FlowEntryRegistryAssembly.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/declarations/UnfinishedBackend.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/diagnostics/DiagnosticFlowEntries.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/host/FlowHostProtocol.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/launcher/BackendExecutionState.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/launcher/BackendRunGate.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/launcher/FlowLaunchParent.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuFlowHostClient.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/shizuku/host/ShizukuBackendHost.kt`

### A4.2 removed duplicate registry members

- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284/BackendCve43284.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/BackendCve43499.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/Profiles43499.kt`

### A4.3 old live data adapter；冻结attack island依赖不在本批删除

- `DELETE app/src/main/kotlin/com/ghostlock/app/data/backend/SharedPreferencesBackendConfigStore.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/data/finish/FinishSettingsAdapter.kt`

## A5. KBR2 DELETE：KBR1后仍无owner的dead island

- `DELETE app/src/main/kotlin/com/ghostlock/app/core/NativeSession.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/SessionPhase.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessCapabilities.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessCapability.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessIo.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessResult.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessSession.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/AccessSessionFactory.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/InitializationReceipt.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/KernelMemoryAccess.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/PageCacheAccess.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/SessionSnapshot.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/access/ShutdownReceipt.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/nativeabi/NativeCallResult.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/nativehandle/NativeHandlePort.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/nativehandle/NativeHandleState.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/nativehandle/OwnedNativeHandle.kt`
- `DELETE app/src/main/kotlin/com/ghostlock/app/core/nativehandle/RunResourceScope.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/access/AccessIoTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/nativeabi/NativeCallResultTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/nativehandle/OwnedNativeHandleTest.kt`

## A6. Existing tests exact disposition

### A6.1 DELETE：只验证旧连接/反射/动态registry/旧DTO

- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/DebugProbeHostSmokeTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43284GluedHostSmokeTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43284TimeoutKeepaliveTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43499GluedHostSmokeTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/FlowHostCancellationTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/FlowHostHostDeathTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/FlowHostKeepaliveTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/FlowHostProcessSmokeTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Real43499GateTest.kt`
- `DELETE app/src/androidTest/kotlin/com/ghostlock/app/backend/core/ShizukuFlowHostSmokeTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43284/BackendCve43284ConfigurationTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/backends/BackendConfigurationCommitOrderingTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/core/FlowEntryParamsTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/core/FlowHostProtocolTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/core/HostProcessExecTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/declarations/BackendFlowSelectionTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/declarations/BackendRegistryInvariantsTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/declarations/FlowEntryRegistryAssemblyTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/declarations/UnfinishedBackendAnnotationTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/launcher/BackendUidRequirementTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/backend/registry/DynamicBackendsTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/flow/FlowEntryRegistryTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/flow/FlowEntryValidationTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/flow/FlowHostParamsTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/core/flow/FlowSymbolTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/data/backend/AndroidBackendConfigurationAdapterTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/data/backend/BackendSessionStoreAuthorityTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/data/backend/BackendSessionStoreLegacySelectionTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/shizuku/ShizukuLauncherSharedTest.kt`
- `DELETE app/src/test/kotlin/com/ghostlock/app/ui/screen/module/backends/BackendParameterSchemaTest.kt`

### A6.2 MODIFY：改吃新合同，不保留旧parity

- `MODIFY app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineCaptureTest.kt`
- `MODIFY app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineFixtures.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/core/flow/FlowRequestTest.kt`：保留typed request、Lua直组与Flow one-shot/stop合同；`backendClass`只作进程内诊断，不进入registry/wire。
- `MODIFY app/src/test/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishScript43284Test.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/ui/screen/module/backends/PayloadEffectsTest.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/ui/screen/module/backends/PayloadRequestsTest.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/ui/screen/module/backends/ScriptRenderTest.kt`

### A6.3 KEEP_NONCOMPAT_REASON：backend-private算法/机制证据，不作为KBR验收

下列文件不测试旧AIDL/registry/UI连接，KBR不改其算法与golden，也不运行它们来宣称backend可用：

- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43284glued/IpSecCliArgumentsTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499RunLogTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499WriteChainFlowTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499RequestTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ExecParamsTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ProfileCodecMergeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimArchitectureTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimSessionTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43284glued/AndroidIpSecRuntimeSmokeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/PerfMechanismProbeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/ActorFutexSelfTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/JniVictimMechanismProbeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Mcast510ProbeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/MondrianConfigProbeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/W1OnlyProbeTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/JniBoundaryContractTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/KernelsnitchBoundsContractTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/KotlinHygieneTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/NativeHygieneContractTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/PselectWindowContractTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/config/Cve43499DebugConfigTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/config/FailStopCpuConfigTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/config/MondrianDebugConfigTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/finish/FinishEnforceRestoreTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499CleanFailureReasonTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499CredentialWriteVectorTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499GeneratorVectorTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/lifecycle/Cve43499LifecycleRootedTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/ActorCpuPinningTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/Cve43499PrebuiltRepairTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/FailStopControllerTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/KernelsuGuardTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/ProcessLimitsTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfOwnerTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfSampleParserTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfTargetSourceTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499JniVictimTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499VictimKillPolicyTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499VictimLaunchSpecFactoryTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/ProcessVictimFinishTimeoutTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/ProcessVictimSessionTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/VictimPipeHighFdTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/VictimProtocolTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499/w3/Cve43499W3HardGateTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimCapabilityTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimProbeAppendTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimProtocolTest.kt`
- `KEEP_NONCOMPAT_REASON app/src/test/kotlin/com/ghostlock/app/core/victim/VictimScriptLauncherTest.kt`

## A7. Architecture、UML与文档action

- `MODIFY tools/kotlin_architecture_d0.py`
- `MODIFY app/src/test/resources/architecture/kotlin-file-actions-v1.tsv`
- `MODIFY app/src/test/resources/architecture/component-api-v1.tsv`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/architecture/ArchitectureInventoryTest.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/architecture/ComponentApiManifestTest.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/architecture/KsuBoundaryLedgerTest.kt`
- `MODIFY app/src/test/kotlin/com/ghostlock/app/architecture/UiSurfaceManifestTest.kt`
- `MODIFY docs/development/full-process-uml.md`：registry class、run state、App/Shizuku sequence、frame/resource ownership四组权威图。
- `MODIFY docs/development/introduction.md`
- `MODIFY README.md`
- `MODIFY README_ZH.md`
- `MODIFY app/src/main/kotlin/com/ghostlock/app/backend/README.md`
- `MODIFY docs/analysis/log-levels-and-sites.md`
- `MODIFY docs/plan/flow-process-launcher-plan.md`
- `MODIFY docs/plan/43499-logging-integration-plan.md`
- `MODIFY docs/plan/kotlin-clean-architecture-ui-plan.md`
- `MODIFY docs/plan/kotlin-core-backend-vertical-rewrite-plan.md`
- `MODIFY docs/plan/kotlin-core-backend-vertical-rewrite-annex-a.md`
- `MODIFY docs/analysis/architecture-review-log.md`
- `KEEP_NONCOMPAT_REASON docs/plan/kotlin-core-observability-cleanup-plan.md`：已标superseded，只留历史，不再继续编辑旧合同。
- `KEEP_NONCOMPAT_REASON docs/plan/kotlin-core-observability-cleanup-annex-a.md`：已标superseded，只留历史，不再继续编辑旧账本。

## A8. 明确冻结的production attack internals

KBR只修改A3.2列出的backend-private caller；下列现有文件保留原算法/常量/资源顺序，不因UI/core整理顺手改动：

- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/host/HostProcessExec.kt`：仅供冻结Glued entry编译；新RunHost不得调用。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/host/HostRunPaths.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/host/FlowEntryParams.kt`：冻结Glued entry与HostRunPaths的常量依赖；新RunHost不得读取。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284/flow/Flow43284Impl.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284/flow/Flow43284Request.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/Flow43284GluedEntry.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Flow43499GluedEntry.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowEntry.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowEntryRegistry.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowHostParams.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowProgress.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowSymbol.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/Flow.kt`：长期`run/stop`合同与进程内实现类诊断保留；registry/wire不得读取`backendClass`。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowRequest.kt`：长期Flow请求标记保留。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowResult.kt`：长期Flow结果保留。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowStep.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/FlowStepRun.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/RetryPolicy.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/Step.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/StepContext.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/StepResult.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/flow/StepTiming.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/finish/FinishConfig.kt`：只供冻结attack island编译；新UI/repository/RunHost不得读取。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/finish/FinishScript.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/finish/FinishScriptStore.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadEffects.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/core/payload/PayloadRequests.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/FileSink.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/LogExport.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/LogFile.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/LogOrigin.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/NativeLogTailer.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/log/log.kt`：只供冻结attack/Lua island；新live path使用EventEmitter。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/FlowLuaRequest.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaNew.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaReflect.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaScriptSelectionStore.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/AndroidIpSecRuntime.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ExecParams.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ProfileCodec.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/request/Cve43499RequestFactory.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/finish/FinishHandoff.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499CommonVictimRuntime.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499JniVictim.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499VictimLaunchSpecFactory.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/ProcessVictimSession.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/VictimProbe.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/jni/Native43499Jni.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/config/Cve43499DebugConfig.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499RunLog.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499Impl.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499Request.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/finish/FinishAcceptance.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/finish/FinishExecutionEvidence.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499CleanFailureReason.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499WriteChain.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Cve43499WriteTransactionRunner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499Layouts.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499Route.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499RunSpec.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/flow/Flow43499WordWrite.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499DebugWritePlanner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499GeneratorSupport.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499MulticastInputGenerator.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499PayloadBuilder.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499PselectInputGenerator.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499RouteGeneratorFactory.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/generator/Cve43499TcpZerocopyInputGenerator.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/lifecycle/Cve43499Lifecycle.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/lifecycle/Cve43499LifecycleApi.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/ActorOwner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/Cve43499PrebuiltRepair.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/FailStopController.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/KernelsuGuard.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/PageOwner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/PlacementOwner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/ProcessLimits.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/owned/TokenOwner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfNative.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfOwner.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfSampleParser.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/perf/Cve43499PerfTargetSource.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/port/Cve43499BackendPort.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/Cve43499VictimKillPolicy.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/ProcessVictim.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/VictimFinish.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/victim/VictimProtocolAppend.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499/w3/Cve43499W3HardGate.kt`
- `KEEP_NONCOMPAT_REASON app/src/main/cpp/**`：附件不展开native树；整棵树无写授权，且KBR old-symbol scanner只扫Kotlin/AIDL，不以native命中扩大范围。
- `KEEP_NONCOMPAT_REASON app/src/main/kotlin/com/ghostlock/app/data/LegacyProfileConverter.kt`
- `KEEP_NONCOMPAT_REASON kernelsnitch/**`

## A9. 批次边界

| 批 | 可写集合 | 结束条件 |
|---|---|---|
| KBR1 | A1、A2、A3、A4、A6、A7中与live cutover直接相关项 | 新registry/wire/Flow/state/event及独立Payload/Finish owner全接线；旧live symbols与caller=0；attack/victim/JNI冻结，exploit/CUSTOM plugins仍Unverified |
| KBR2 | A5与A7的dead-island manifest/UML收口 | 只删除compile-time unreachable定义/专属tests；runtime control-flow diff=0 |
| KBR3 | A7文档authority项 | README双语、backend README、logging、UML、计划指针一致；不首次改production |
