#!/usr/bin/env python3
"""Generate and verify the Kotlin Clean Architecture D0 characterization data.

The tool is intentionally the single operational entry for D0.  Inventory and
verification are host-only; capture is the only subcommand that may manage the
dedicated read-only AVD.  Production sources are never modified.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
from typing import Iterable, Sequence


ARCHITECTURE_RULES = (
    "ui-implementation-dependency",
    "data-backend-concrete-implementation",
    "core-concrete-backend-implementation",
    "backend-data-cve-reverse-dependency",
    "backend-declaration-concrete-implementation",
    "backend-product-script-authority",
    "runtime-reflection",
    "superseded-run-log-topology",
    "runhost-client-log-sink-owner",
    "old-aidl-transaction",
    "manual-jni-object-marshalling",
)

REFLECTION_FORBIDDEN_PREFIXES = (
    "app/src/main/kotlin/com/ghostlock/app/backend/",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/",
    "app/src/main/kotlin/com/ghostlock/app/core/run/",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/",
    "app/src/main/kotlin/com/ghostlock/app/data/run/",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/",
    "app/src/main/kotlin/com/ghostlock/app/shizuku/",
)
REFLECTION_FORBIDDEN_FILES = {
    "app/src/main/kotlin/com/ghostlock/app/AppContainer.kt",
}


TSV_NAMES = (
    "kotlin-file-actions-v1.tsv",
    "component-api-v1.tsv",
    "ksu-ui-remove-v1.tsv",
    "ksu-exec-keep-v1.tsv",
    "ui-surface-matrix-v1.tsv",
)

KSU_TOKENS = (
    "KernelSU",
    "KSU",
    "ksuApp",
    "Natives",
    "ksud",
    "libksud",
    "/data/adb/ksu",
    "/data/adb/ksud",
    "/data/adb/modules",
    "/sys/module/ksu",
    "/sys/module/kernelsu",
    "/proc/modules",
    "late-load",
    "safe-mode",
)

KSU_IDENTIFIER_PATTERN = re.compile(r"(?<![A-Za-z0-9_])KSU(?![A-Za-z0-9_])")
KSU_SYMBOL_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_])(?:ksu[A-Z_][A-Za-z0-9_]*|Kernelsu[A-Z_][A-Za-z0-9_]*)",
)


def contains_ksu_token(text: str) -> bool:
    """Return whether text contains one configured KSU token without identifier substrings."""
    return KSU_SYMBOL_PATTERN.search(text) is not None or any(
        KSU_IDENTIFIER_PATTERN.search(text) is not None
        if token == "KSU" else token in text
        for token in KSU_TOKENS
    )

DELETE_KOTLIN = {
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/superuser/SuperUserMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/superuser/SuperUserMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/superuser/SuperUserScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/superuser/SuperUserUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/appprofile/AppProfileMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/appprofile/AppProfileMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/appprofile/AppProfileScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/appprofile/AppProfileUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/appprofile/AppProfileUtils.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/sulog/SulogMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/sulog/SulogMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/sulog/SulogScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/sulog/SulogUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/template/TemplateMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/template/TemplateMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/template/TemplateScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/template/TemplateUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/templateeditor/TemplateEditorMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/templateeditor/TemplateEditorMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/templateeditor/TemplateEditorScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/templateeditor/TemplateEditorUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/templateeditor/TemplateEditorUtils.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/executemoduleaction/ExecuteModuleActionMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/executemoduleaction/ExecuteModuleActionMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/executemoduleaction/ExecuteModuleActionScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/executemoduleaction/ExecuteModuleActionUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/executemoduleaction/ExecuteModuleActionUtils.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/scriptrun/ScriptRunMaterial.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/scriptrun/ScriptRunMiuix.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/scriptrun/ScriptRunScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/scriptrun/ScriptRunUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/ModuleScreen.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/ModuleUiState.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/ShortcutType.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/BuildConfig.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/KsuApp.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/KsuService.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/Natives.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/model/AppInfo.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/model/Module.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/model/ModuleUpdateInfo.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/model/TemplateInfo.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/ModuleRepository.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/ModuleRepositoryImpl.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/SuperUserRepository.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/SuperUserRepositoryImpl.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/TemplateRepository.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/repository/TemplateRepositoryImpl.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/script/ScriptCommand.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/script/ScriptPackage.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/script/ScriptParams.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/script/ScriptRepository.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/data/script/ScriptRunRecord.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/theme/GhostlockTheme.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/theme/ThemeSettings.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/KsuCli.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/Network.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/PinyinUtil.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/Serialization.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/ShellHighlight.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/SulogHelper.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/UidGroupUtils.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/util/module/Shortcut.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/ModuleViewModel.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/ScriptRunViewModel.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/SulogViewModel.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/SuperUserViewModel.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/TemplateViewModel.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/navigation3/IntentDispatcher.kt",
}

DELETE_AIDL = "app/src/main/aidl/com/ghostlock/app/ui/IKsuInterface.aidl"

KEEP_KOTLIN = {
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/PayloadScriptRenderer.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/PayloadVerifier.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/PublishedArtifactIdentity.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/RootScriptArtifact.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/RootScriptRenderer.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/RootScriptResult.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/RootScriptResultCodec.kt",
    "app/src/main/kotlin/com/ghostlock/app/data/diagnostics/AndroidDiagnosticReportBuilder.kt",
    "app/src/main/kotlin/com/ghostlock/app/data/run/RootManagerArtifactResolver.kt",
    "app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadEditor.kt",
    "app/src/main/kotlin/com/ghostlock/app/domain/payload/PayloadDefinition.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/payload/PayloadUi.kt",
}

KEEP_TESTS = {
    "app/src/test/kotlin/com/ghostlock/app/core/rootscript/PayloadScriptRendererTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/rootscript/PayloadVerifierTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/rootscript/PublishedArtifactIdentityTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/rootscript/RootScriptRendererTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/rootscript/RootScriptResultCodecTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/data/run/RootManagerArtifactResolverTest.kt",
}

KEEP_NATIVE = {
    "app/src/main/assets/kernel_profiles/6.6.30-android15-8-g24ffe62b0a0e-abogki387229724-4k.conf",
    "app/src/main/cpp/backends/43284glued/SOURCE-MANIFEST.json",
    "app/src/main/cpp/backends/43284glued/bootstrap.c",
    "app/src/main/cpp/backends/43284glued/exp.c",
    "app/src/main/cpp/backends/43499glued/attack/ops.cpp",
    "app/src/main/cpp/backends/43499glued/main.cpp",
    "app/src/main/cpp/backends/43499glued/route/frontend_contract.hpp",
    "app/src/main/cpp/backends/43499glued/route/pipeline.hpp",
    "app/src/main/cpp/backends/43499glued/session/backend/cve_2026_43499_backend.cpp",
    "app/src/main/cpp/backends/43499glued/session/exploit_session.hpp",
    "app/src/main/cpp/backends/43499glued/session/handoff_probe.cpp",
    "app/src/main/cpp/backends/43499glued/session/handoff_probe.hpp",
    "app/src/main/cpp/backends/43499glued/session/root_child_frontend.cpp",
    "app/src/main/cpp/backends/43499glued/session/root_child_frontend.hpp",
    "app/src/main/cpp/backends/43499glued/session/runtime_config.cpp",
    "app/src/main/cpp/backends/43499glued/session/runtime_config.h",
    "app/src/main/cpp/backends/43499glued/session/victim_process.cpp",
    "app/src/main/cpp/backends/43499glued/tests/handoff_probe_test.cpp",
    "app/src/main/cpp/backends/43499glued/tests/host/handoff_stub.cpp",
    "app/src/main/cpp/backends/98260/CMakeLists.txt",
    "app/src/main/cpp/backends/98260/README.md",
    "app/src/main/cpp/backends/98260/timer/a301so_515_params.h",
    "app/src/main/cpp/backends/98260/timer/avd_61_params.h",
    "app/src/main/cpp/backends/98260/timer/target_params.h",
    "app/src/main/cpp/backends/98260/timer/timer.c",
}

ADD_TARGETS = {
    "app/src/main/kotlin/com/ghostlock/app/data/run/AndroidRunLogSinkFactory.kt": "R4B",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunLogDocumentFormatter.kt": "R4A",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunLogSink.kt": "R4B",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/UiPreferences.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/UiPreferencesPort.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/ObserveUiPreferencesUseCase.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/device/DeviceStatus.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/device/DeviceStatusPort.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/device/ReadDeviceStatusUseCase.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRuntimeSpec.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRegistrySnapshot.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendCatalogPort.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/GetBackendCatalogUseCase.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/data/settings/SharedPreferencesUiPreferencesRepository.kt": "D1b",
    "app/src/main/kotlin/com/ghostlock/app/data/device/AndroidDeviceStatusAdapter.kt": "D1c",
    "app/src/main/kotlin/com/ghostlock/app/data/device/KernelVersionReader.kt": "D1c",
    "app/src/main/kotlin/com/ghostlock/app/data/device/SELinuxStatusReader.kt": "D1c",
    "app/src/main/kotlin/com/ghostlock/app/AppContainer.kt": "D1d",
    "app/src/main/kotlin/com/ghostlock/app/AppViewModelFactory.kt": "D1d",
    "app/src/main/kotlin/com/ghostlock/app/ui/component/CapabilityGate.kt": "D2c",
    "app/src/main/kotlin/com/ghostlock/app/ui/component/profile/ProfileDraft.kt": "D2b",
    "app/src/main/kotlin/com/ghostlock/app/ui/component/profile/Capabilities.kt": "D2b",
    "app/src/main/kotlin/com/ghostlock/app/ui/component/profile/Groups.kt": "D2b",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunCommand.kt": "D5a",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunSessionState.kt": "D5a",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunSessionPort.kt": "D5a",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunBackendUseCase.kt": "D5a",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/RunCoordinator.kt": "D5b",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/BackendCatalogAdapter.kt": "D5e",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/BackendPresentationAdapter.kt": "D5e",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/BackendRunSessionAdapter.kt": "D5b",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/run/RunUiState.kt": "D5c",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/run/RunActions.kt": "D5c",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/RunViewModel.kt": "D5c",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferences.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferencesPort.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/OperationalPreferencesUseCases.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/data/settings/SharedPreferencesOperationalPreferencesAdapter.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRegistryIdentity.kt": "D5e",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendRegistryIdentityFactory.kt": "D5e",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaScriptSelectionStore.kt": "D5e",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendConfig.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendExecution.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendFlowParams.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendParameterSchema.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendReadiness.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/BackendExecutionState.kt": "D5b",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/BackendRunGate.kt": "D5b",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/FlowLaunchParent.kt": "D5b",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/AndroidBackendConfigurationAdapter.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSelectionAdapter.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/BackendSessionStore.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/SharedPreferencesBackendConfigStore.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/data/device/AndroidDeviceNameReader.kt": "D1c",
    "app/src/main/kotlin/com/ghostlock/app/data/diagnostics/AndroidDiagnosticReportBuilder.kt": "D2c",
    "app/src/main/kotlin/com/ghostlock/app/data/finish/FinishEditorAdapter.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/data/finish/FinishSettingsAdapter.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/data/privilege/AndroidPrivilegeAdapter.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/data/update/DisabledAppUpdateAdapter.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendConfiguration.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendSelectionPort.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/SelectBackendUseCase.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/domain/diagnostics/CreateDiagnosticReportUseCase.kt": "D2c",
    "app/src/main/kotlin/com/ghostlock/app/domain/diagnostics/DiagnosticReportPort.kt": "D2c",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishEditor.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishEditorPort.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishSettings.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/FinishSettingsPort.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/PreviewFinishScriptUseCase.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/ReadFinishEditorUseCase.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/ReadFinishSettingsUseCase.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/SaveFinishEditorUseCase.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/finish/SaveFinishSettingsUseCase.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/domain/privilege/PrivilegePort.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/privilege/PrivilegeState.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/privilege/PrivilegeUseCases.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/SaveUiPreferencesUseCase.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/settings/SetNavigationRailExpandedUseCase.kt": "D1a",
    "app/src/main/kotlin/com/ghostlock/app/domain/update/AppUpdateInfo.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/update/AppUpdatePort.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/domain/update/CheckAppUpdateUseCase.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendConfigurationSpec.kt": "D7.1",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendToggleConfigCard.kt": "D7.2",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendChoiceDocumentConfigCard.kt": "D7.2",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendDocumentConfigCard.kt": "D7.2",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendConfigRenderer.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendStatusTone.kt": "D4a",
    "app/src/main/kotlin/com/ghostlock/app/ui/kit/ConfigItem.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/ui/platform/PredictiveBack.kt": "D6",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/BackendPageUiState.kt": "D4b",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishEditorUiState.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishSettingsUiState.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/FinishEditorViewModel.kt": "D4f",
    "app/src/main/kotlin/com/ghostlock/app/ui/viewmodel/FinishSettingsViewModel.kt": "D4f",
    "app/src/main/aidl/com/ghostlock/app/run/FinishItemParcel.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/IRunHost.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/IRunHostCallback.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/ItemResultParcel.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/PayloadItemParcel.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/RunHostLogRecord.aidl": "R4C",
    "app/src/main/aidl/com/ghostlock/app/run/RunHostRequest.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/RunHostSnapshot.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/RunHostSnapshotReply.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/RunInputArtifactParcel.aidl": "R3",
    "app/src/main/aidl/com/ghostlock/app/run/StageResultParcel.aidl": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendPlugin.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/registry/BackendRegistry.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/AppRunHostClient.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/BackendProcessCarrier.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostLogCursor.kt": "R4C",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostExecutionDomainVerifier.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostParcelBudget.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostProtocol.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostRequest.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostService.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostSnapshot.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostSnapshotPoller.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostSnapshotReply.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/ShizukuRunHostClient.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/BackendFlowInput.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/BackendRunFacts.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/core/run/RunCoordinator.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/core/run/RunInputFreezer.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/data/run/AndroidRunWorkspace.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/data/run/RootArtifactStagingAdapter.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendSpec.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/FrozenRunInput.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunLogRecord.kt": "R4C",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunReport.kt": "R3",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendCarrierArtifact.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostProductPipeline.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/PublishedFlowArtifact.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/core/rootscript/RootScriptExecutionPlan.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/core/run/FrozenRunInputLease.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/RunReportComposer.kt": "R4C",
    "app/src/main/kotlin/com/ghostlock/app/ui/screen/run/AnsiRunLogText.kt": "R4C",
    "app/src/test/kotlin/com/ghostlock/app/ui/screen/run/AnsiRunLogTextTest.kt": "R4C",
    "app/src/main/kotlin/com/ghostlock/app/data/run/PackagedBackendArtifactResolver.kt": "R5-S1",
    "app/src/main/kotlin/com/ghostlock/app/data/run/RootManagerArtifactResolver.kt": "R5-S1",
}

AUTOMATIC_LOG_TEST_TARGETS = {
    "app/src/androidTest/kotlin/com/ghostlock/app/data/run/AndroidRunLogSinkFactoryTest.kt",
    "app/src/androidTest/kotlin/com/ghostlock/app/data/settings/OperationalPreferencesExportContractTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/run/RunCoordinatorLogSinkTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/domain/run/RunLogDocumentFormatterTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/ui/screen/run/AnsiRunLogTextTest.kt",
}

ADD_TARGETS.update({path: "R4B" for path in AUTOMATIC_LOG_TEST_TARGETS})
ADD_TARGETS["app/src/androidTest/kotlin/com/ghostlock/app/data/settings/OperationalPreferencesExportContractTest.kt"] = "R4A"
ADD_TARGETS["app/src/test/kotlin/com/ghostlock/app/domain/run/RunLogDocumentFormatterTest.kt"] = "R4A"

D7_FILE_MOVES = {
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/Backend43284ConfigCard.kt":
        "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendToggleConfigCard.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/Backend43499GluedProfileCard.kt":
        "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendChoiceDocumentConfigCard.kt",
    "app/src/main/kotlin/com/ghostlock/app/ui/component/backend/CustomLuaConfig.kt":
        "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendDocumentConfigCard.kt",
}

D7_FILE_DELETES = {
    "app/src/main/kotlin/com/ghostlock/app/core/finish/FinishExecution.kt",
}

R3_FILE_DELETES = {
    "app/src/androidTest/kotlin/com/ghostlock/app/backend/backends/cve43284glued/AndroidIpSecRuntimeSmokeTest.kt",
    "app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43284GluedHostSmokeTest.kt",
    "app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43284TimeoutKeepaliveTest.kt",
    "app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Flow43499GluedHostSmokeTest.kt",
    "app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Real43499GateTest.kt",
    "app/src/main/aidl/com/ghostlock/app/flow/IFlowHost.aidl",
    "app/src/main/aidl/com/ghostlock/app/flow/IFlowHostCallback.aidl",
    "app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockCallback.aidl",
    "app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockUserService.aidl",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/AndroidIpSecRuntime.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/BackendCve43284Glued.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43284glued/Flow43284GluedEntry.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/BackendCve43499Glued.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Flow43499GluedEntry.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ExecParams.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ProfileCodec.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaNew.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/backends/lua/LuaReflect.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendFlowParams.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/BackendRegistryIdentityFactory.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/FlowEntryRegistryAssembly.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/declarations/UnfinishedBackend.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/diagnostics/DiagnosticFlowEntries.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/host/FlowEntryParams.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/host/FlowHostProtocol.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/host/HostRunPaths.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/BackendExecutionState.kt",
    "app/src/main/kotlin/com/ghostlock/app/backend/launcher/FlowLaunchParent.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/FlowEntry.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/FlowEntryRegistry.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/FlowHostParams.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/FlowSymbol.kt",
    "app/src/main/kotlin/com/ghostlock/app/data/backend/SharedPreferencesBackendConfigStore.kt",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRegistryIdentity.kt",
    "app/src/main/kotlin/com/ghostlock/app/domain/backend/BackendRuntimeSpec.kt",
    "app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuBackendClient.kt",
    "app/src/main/kotlin/com/ghostlock/app/shizuku/ShizukuFlowHostClient.kt",
    "app/src/main/kotlin/com/ghostlock/app/shizuku/host/ShizukuBackendHost.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/backends/BackendConfigurationCommitOrderingTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43284glued/IpSecCliArgumentsTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ExecParamsTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/backends/cve43499glued/Glued43499ProfileCodecMergeTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/core/FlowEntryParamsTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/declarations/FlowEntryRegistryAssemblyTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/declarations/UnfinishedBackendAnnotationTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/backend/registry/DynamicBackendsTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/flow/FlowEntryRegistryTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/flow/FlowEntryValidationTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/flow/FlowHostParamsTest.kt",
    "app/src/test/kotlin/com/ghostlock/app/core/flow/FlowSymbolTest.kt",
}

R4_FILE_DELETES = {
    "app/src/main/kotlin/com/ghostlock/app/core/SessionPhase.kt",
    "app/src/main/aidl/com/ghostlock/app/run/RunHostEvent.aidl",
    "app/src/main/kotlin/com/ghostlock/app/backend/transport/RunHostEventCursor.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/flow/FlowProgress.kt",
    "app/src/main/kotlin/com/ghostlock/app/core/run/RunReportComposer.kt",
    "app/src/main/kotlin/com/ghostlock/app/domain/run/RunEvent.kt",
    "app/src/main/kotlin/com/ghostlock/app/log/FileSink.kt",
    "app/src/main/kotlin/com/ghostlock/app/log/LogFile.kt",
    "app/src/main/kotlin/com/ghostlock/app/log/LogOrigin.kt",
    "app/src/main/kotlin/com/ghostlock/app/log/log.kt",
    "app/src/main/kotlin/com/ghostlock/app/log/LogExport.kt",
}

OLD_AIDL_CONTRACTS = {
    "IFlowHost": "app/src/main/aidl/com/ghostlock/app/flow/IFlowHost.aidl",
    "IFlowHostCallback": "app/src/main/aidl/com/ghostlock/app/flow/IFlowHostCallback.aidl",
    "IGhostlockCallback": "app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockCallback.aidl",
    "IGhostlockUserService": "app/src/main/aidl/com/ghostlock/app/shizuku/IGhostlockUserService.aidl",
}

MANUAL_JNI_EXEMPT_PREFIXES = (
    "app/src/main/cpp/backends/43284glued/",
    "app/src/main/cpp/backends/43499glued/",
    "app/src/main/cpp/backends/98260/",
)

D7_COMPONENT_SOURCE_PATH = (
    "app/src/main/kotlin/com/ghostlock/app/ui/component/backend/CustomLuaConfig.kt"
)
D7_COMPONENT_TARGET_PATH = (
    "app/src/main/kotlin/com/ghostlock/app/ui/backend/BackendDocumentConfigCard.kt"
)
D7_COMPONENT_SOURCE_API_KEY = (
    "fun|com.ghostlock.app.ui.component.backend|CustomLuaConfig||0|"
    "BackendConfiguration.Document,(String) -> BackendConfigMutationResult,"
)
D7_COMPONENT_TARGET_API_KEY = (
    "fun|com.ghostlock.app.ui.backend|BackendDocumentConfigCard||0|"
    "BackendConfiguration.Document,(String) -> BackendConfigMutationResult,"
)
D7_COMPONENT_SOURCE_FILE_SHA = "8b9285776b92e126e0f28c8d4427440fd01339ca9a4342d56c383d8c2d6e0fa0"
D7_COMPONENT_SOURCE_API_SHA = "fed260444aac2910814d014f58c1ee7bbd4523af80d64337a8e54c19c3bc89b6"

INPUTIZE_COMPONENT_PARTS = (
    "/component/CapabilityGate.kt",
    "/component/bottombar/",
    "/component/markdown/GithubMarkdown.kt",
    "/component/profile/",
    "/component/rebootlistpopup/",
    "/component/material/SendLogBottomSheet.kt",
    "/component/miuix/SendLogDialog.kt",
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp_name, path)
        directory_fd = os.open(path.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


def tsv_bytes(fieldnames: Sequence[str], rows: Iterable[dict[str, object]]) -> bytes:
    output = io.StringIO(newline="")
    writer = csv.DictWriter(output, fieldnames=fieldnames, delimiter="\t", lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow({name: row.get(name, "") for name in fieldnames})
    # Preserve an empty final TSV field explicitly without leaving trailing whitespace.
    return output.getvalue().replace("\t\n", '\t""\n').encode("utf-8")


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def relative(path: Path, repo: Path) -> str:
    return path.relative_to(repo).as_posix()


def owner_for(path: Path, text: str) -> str:
    match = re.search(r"(?m)^\s*package\s+([\w.]+)", text)
    return match.group(1) if match else path.parent.as_posix()


def strip_kotlin(text: str) -> str:
    """Replace comments and string/character contents while preserving newlines."""
    output = list(text)
    index = 0
    state = "code"
    block_depth = 0
    while index < len(text):
        if state == "code":
            if text.startswith("//", index):
                output[index] = output[index + 1] = " "
                index += 2
                state = "line"
                continue
            if text.startswith("/*", index):
                output[index] = output[index + 1] = " "
                index += 2
                state = "block"
                block_depth = 1
                continue
            if text.startswith('"""', index):
                output[index:index + 3] = "   "
                index += 3
                state = "triple"
                continue
            if text[index] == '"':
                output[index] = " "
                index += 1
                state = "string"
                continue
            if text[index] == "'":
                output[index] = " "
                index += 1
                state = "char"
                continue
            index += 1
            continue
        if state == "line":
            if text[index] == "\n":
                state = "code"
            else:
                output[index] = " "
            index += 1
            continue
        if state == "block":
            if text.startswith("/*", index):
                output[index:index + 2] = "  "
                block_depth += 1
                index += 2
            elif text.startswith("*/", index):
                output[index:index + 2] = "  "
                block_depth -= 1
                index += 2
                if block_depth == 0:
                    state = "code"
            else:
                if text[index] != "\n":
                    output[index] = " "
                index += 1
            continue
        delimiter = '"' if state == "string" else "'"
        if state == "triple":
            if text.startswith('"""', index):
                output[index:index + 3] = "   "
                index += 3
                state = "code"
            else:
                if text[index] != "\n":
                    output[index] = " "
                index += 1
            continue
        if text[index] == "\\":
            output[index] = " "
            if index + 1 < len(text):
                if text[index + 1] != "\n":
                    output[index + 1] = " "
                index += 2
            else:
                index += 1
        elif text[index] == delimiter:
            output[index] = " "
            index += 1
            state = "code"
        else:
            if text[index] != "\n":
                output[index] = " "
            index += 1
    return "".join(output)


DECLARATION_RE = re.compile(
    r"(?m)^[ \t]*(?P<mods>(?:(?:public|internal|private|protected|data|sealed|enum|annotation|value|open|abstract|final|override|operator|infix|tailrec|suspend|inline|const|lateinit|external|expect|actual)\s+)*)"
    r"(?P<kind>class|interface|object|typealias|fun|val|var)\s+"
    r"(?P<head>[^\n{};]+)",
)


def matching_brace(text: str, opening: int) -> int | None:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return None


def declaration_terminator(clean: str, start: int) -> tuple[int, str]:
    paren = bracket = angle = 0
    index = start
    while index < len(clean):
        char = clean[index]
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(0, paren - 1)
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket = max(0, bracket - 1)
        elif char == "<":
            angle += 1
        elif char == ">":
            angle = max(0, angle - 1)
        elif paren == 0 and bracket == 0 and angle == 0 and char in "{=;":
            return index, char
        elif paren == 0 and bracket == 0 and angle == 0 and char == "\n":
            candidate = clean[start:index]
            if "(" not in candidate or candidate.rstrip().endswith(")"):
                return index, char
        index += 1
    return index, ""


def declaration_header(clean: str, start: int) -> str:
    end, _ = declaration_terminator(clean, start)
    return " ".join(clean[start:end].split())


def function_body_ranges(clean: str) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for match in DECLARATION_RE.finditer(clean):
        if match.group("kind") != "fun":
            continue
        terminal, character = declaration_terminator(clean, match.start())
        if character == "{":
            closing = matching_brace(clean, terminal)
            if closing is not None:
                ranges.append((terminal, closing))
    return ranges


def class_ranges(clean: str, package: str) -> list[tuple[int, int, str, bool]]:
    raw: list[tuple[int, int, str, bool]] = []
    for match in DECLARATION_RE.finditer(clean):
        if match.group("kind") not in {"class", "interface", "object"}:
            continue
        hidden = any(mod in match.group("mods").split() for mod in ("private", "protected"))
        name_match = re.match(r"([A-Za-z_]\w*)", match.group("head").strip())
        if not name_match:
            continue
        terminal, character = declaration_terminator(clean, match.start())
        if character == "{":
            closing = matching_brace(clean, terminal)
        else:
            closing = terminal
        if closing is not None:
            raw.append((match.start(), closing, name_match.group(1), hidden))
    result: list[tuple[int, int, str, bool]] = []
    for opening, closing, name, hidden in raw:
        parents = [item for item in raw if item[0] < opening < item[1] and item != (opening, closing, name, hidden)]
        parents.sort(key=lambda item: item[0])
        inherited_hidden = hidden or any(item[3] for item in parents)
        result.append((opening, closing, package + "." + ".".join([item[2] for item in parents] + [name]), inherited_hidden))
    return result


def split_parameters(parameter_text: str) -> list[str]:
    if not parameter_text.strip():
        return []
    parts: list[str] = []
    start = 0
    depths = [0, 0, 0]
    for index, char in enumerate(parameter_text):
        if char == "(": depths[0] += 1
        elif char == ")": depths[0] -= 1
        elif char == "<": depths[1] += 1
        elif char == ">": depths[1] -= 1
        elif char == "[": depths[2] += 1
        elif char == "]": depths[2] -= 1
        elif char == "," and depths == [0, 0, 0]:
            parts.append(parameter_text[start:index].strip())
            start = index + 1
    parts.append(parameter_text[start:].strip())
    return [part for part in parts if part]


def normalized_parameter_types(header: str) -> str:
    opening = header.find("(")
    if opening < 0:
        return ""
    depth = 0
    closing = -1
    for index in range(opening, len(header)):
        if header[index] == "(": depth += 1
        elif header[index] == ")":
            depth -= 1
            if depth == 0:
                closing = index
                break
    if closing < 0:
        return ""
    result = []
    for parameter in split_parameters(header[opening + 1:closing]):
        parameter = re.sub(r"^\s*(?:crossinline|noinline|vararg)\s+", "", parameter)
        parameter = parameter.split("=", 1)[0].strip()
        if ":" in parameter:
            parameter = parameter.split(":", 1)[1].strip()
        result.append(" ".join(parameter.split()))
    return ",".join(result)


def component_rows(repo: Path) -> list[dict[str, object]]:
    root = repo / "app/src/main/kotlin/com/ghostlock/app/ui/component"
    files = sorted(root.rglob("*.kt"))
    d7_target = repo / D7_COMPONENT_TARGET_PATH
    if d7_target.is_file():
        files.append(d7_target)
    files = sorted(set(files))
    rows: list[dict[str, object]] = []
    for path in files:
        rel = relative(path, repo)
        text = path.read_text(encoding="utf-8")
        package_match = re.search(r"(?m)^\s*package\s+([\w.]+)", text)
        package = package_match.group(1) if package_match else ""
        d7_component_target = rel == D7_COMPONENT_TARGET_PATH
        file_action = "rename-target-1to1" if d7_component_target else "preserve-path"
        rows.append({
            "row_type": "file",
            "path": rel,
            "kind": "file",
            "owner_fqn": package,
            "name": path.name,
            "receiver_type": "",
            "type_parameter_arity": 0,
            "normalized_parameter_types": "",
            "api_key": "file|" + rel,
            "old_signature_sha": sha256_file(path),
            "action": file_action,
            "target_api_key": "file|" + rel,
            "target_signature": rel,
            "parameter_mapping": "identity",
            "default_mapping": "identity",
            "caller_manifest": "compile-reference-fixture",
            "batch": "D7.2" if d7_component_target else (
                "D2" if any(part in "/" + rel for part in INPUTIZE_COMPONENT_PARTS) else "FROZEN"
            ),
            "evidence": (
                "rename-target-from:" + D7_COMPONENT_SOURCE_PATH
                if d7_component_target else "baseline-component-file"
            ),
            "source_line": 1,
            "declared_type": "",
            "mutability": "",
        })
        clean = strip_kotlin(text)
        function_ranges = function_body_ranges(clean)
        owners = class_ranges(clean, package)
        for match in DECLARATION_RE.finditer(clean):
            if any(start < match.start() < end for start, end in function_ranges):
                continue
            modifiers = set(match.group("mods").split())
            if "private" in modifiers or "protected" in modifiers:
                continue
            kind = match.group("kind")
            head = match.group("head").strip()
            header = declaration_header(clean, match.start())
            if not header:
                continue
            owner_candidates = [item for item in owners if item[0] < match.start() < item[1]]
            owner_candidates.sort(key=lambda item: item[0])
            if any(item[3] for item in owner_candidates):
                continue
            leading = re.match(r"[ \t]*", match.group(0)).group(0)
            indentation = sum(4 if character == "\t" else 1 for character in leading)
            if indentation > 4 * len(owner_candidates):
                continue
            owner = owner_candidates[-1][2] if owner_candidates else package
            receiver = ""
            name = ""
            if kind == "fun":
                before_paren = head.split("(", 1)[0].strip()
                before_paren = re.sub(r"^<[^>]+>\s*", "", before_paren)
                if "." in before_paren:
                    receiver, name = before_paren.rsplit(".", 1)
                else:
                    name = before_paren
            else:
                name_match = re.match(r"([A-Za-z_]\w*)", head)
                if name_match:
                    name = name_match.group(1)
            if not name:
                continue
            parameter_types = normalized_parameter_types(header) if kind in {"fun", "class"} else ""
            type_parameters = re.search(r"<([^>]*)>", header.split("(", 1)[0])
            arity = len(split_parameters(type_parameters.group(1))) if type_parameters else 0
            api_key = "|".join((kind, owner, name, receiver, str(arity), parameter_types))
            normalized = " ".join(header.split())
            line = text.count("\n", 0, match.start()) + 1
            inputize = any(part in "/" + rel for part in INPUTIZE_COMPONENT_PARTS)
            action = (
                "rename-target-1to1" if d7_component_target else
                ("inputize-signature-1to1" if inputize else "preserve-exact")
            )
            rows.append({
                "row_type": "api",
                "path": rel,
                "kind": kind,
                "owner_fqn": owner,
                "name": name,
                "receiver_type": receiver,
                "type_parameter_arity": arity,
                "normalized_parameter_types": parameter_types,
                "api_key": api_key,
                "old_signature_sha": sha256_bytes(normalized.encode()),
                "action": action,
                "target_api_key": api_key,
                "target_signature": normalized if not inputize else "G1_INPUTIZE:" + normalized,
                "parameter_mapping": "identity" if not inputize else "existing-parameters+explicit-capability",
                "default_mapping": "identity",
                "caller_manifest": "compile-reference-fixture",
                "batch": "D7.2" if d7_component_target else ("D2" if inputize else "FROZEN"),
                "evidence": (
                    "rename-target-from:" + D7_COMPONENT_SOURCE_API_KEY
                    if d7_component_target else "source-declaration"
                ),
                "source_line": line,
                "declared_type": "" if kind not in {"val", "var", "typealias"} else normalized,
                "mutability": kind if kind in {"val", "var"} else "",
            })
            if kind == "class" and "(" in header:
                constructor_key = "|".join(("constructor", owner + "." + name, "<init>", "", str(arity), parameter_types))
                rows.append({
                    "row_type": "api",
                    "path": rel,
                    "kind": "constructor",
                    "owner_fqn": owner + "." + name,
                    "name": "<init>",
                    "receiver_type": "",
                    "type_parameter_arity": arity,
                    "normalized_parameter_types": parameter_types,
                    "api_key": constructor_key,
                    "old_signature_sha": sha256_bytes(normalized.encode()),
                    "action": action,
                    "target_api_key": constructor_key,
                    "target_signature": normalized if not inputize else "G1_INPUTIZE:" + normalized,
                    "parameter_mapping": "identity" if not inputize else "existing-parameters+explicit-capability",
                    "default_mapping": "identity",
                    "caller_manifest": "compile-reference-fixture",
                    "batch": "D7.2" if d7_component_target else ("D2" if inputize else "FROZEN"),
                    "evidence": "primary-constructor",
                    "source_line": line,
                    "declared_type": "",
                    "mutability": "",
                })
    target_api = next(
        (row for row in rows if row.get("api_key") == D7_COMPONENT_TARGET_API_KEY),
        None,
    )
    if target_api is None:
        raise ValueError("D7 component target API missing: " + D7_COMPONENT_TARGET_API_KEY)
    rows.extend([
        {
            "row_type": "file",
            "path": D7_COMPONENT_SOURCE_PATH,
            "kind": "file",
            "owner_fqn": "com.ghostlock.app.ui.component.backend",
            "name": "CustomLuaConfig.kt",
            "receiver_type": "",
            "type_parameter_arity": 0,
            "normalized_parameter_types": "",
            "api_key": "file|" + D7_COMPONENT_SOURCE_PATH,
            "old_signature_sha": D7_COMPONENT_SOURCE_FILE_SHA,
            "action": "rename-source-1to1",
            "target_api_key": "file|" + D7_COMPONENT_TARGET_PATH,
            "target_signature": D7_COMPONENT_TARGET_PATH,
            "parameter_mapping": "identity",
            "default_mapping": "identity",
            "caller_manifest": "compile-reference-fixture",
            "batch": "D7.2",
            "evidence": "retired-component-source",
            "source_line": 1,
            "declared_type": "",
            "mutability": "",
        },
        {
            "row_type": "api",
            "path": D7_COMPONENT_SOURCE_PATH,
            "kind": "fun",
            "owner_fqn": "com.ghostlock.app.ui.component.backend",
            "name": "CustomLuaConfig",
            "receiver_type": "",
            "type_parameter_arity": 0,
            "normalized_parameter_types": (
                "BackendConfiguration.Document,(String) -> BackendConfigMutationResult"
                ","
            ),
            "api_key": D7_COMPONENT_SOURCE_API_KEY,
            "old_signature_sha": D7_COMPONENT_SOURCE_API_SHA,
            "action": "rename-source-1to1",
            "target_api_key": D7_COMPONENT_TARGET_API_KEY,
            "target_signature": target_api["target_signature"],
            "parameter_mapping": "identity",
            "default_mapping": "identity",
            "caller_manifest": "compile-reference-fixture",
            "batch": "D7.2",
            "evidence": "retired-component-api",
            "source_line": 28,
            "declared_type": "",
            "mutability": "",
        },
    ])
    return rows


def kotlin_source_paths(repo: Path) -> list[Path]:
    return sorted((repo / "app/src/main/kotlin").rglob("*.kt"))


def is_reflection_forbidden_scope(relative_path: str) -> bool:
    """Return whether one source belongs to the typed backend execution spine."""
    return relative_path in REFLECTION_FORBIDDEN_FILES or relative_path.startswith(
        REFLECTION_FORBIDDEN_PREFIXES
    )


def architecture_violations(repo: Path) -> list[str]:
    """Return forbidden Kotlin, AIDL, and native edges with stable source locations."""
    source_prefix = "app/src/main/kotlin/com/ghostlock/app/"
    import_pattern = re.compile(r"(?m)^\s*import\s+([A-Za-z_][A-Za-z0-9_.]*)")
    violations: list[str] = []

    for path in kotlin_source_paths(repo):
        rel = relative(path, repo)
        if not rel.startswith(source_prefix):
            continue
        layer = rel.removeprefix(source_prefix).split("/", 1)[0]
        stripped = strip_kotlin(path.read_text(encoding="utf-8"))
        reflection_forbidden = is_reflection_forbidden_scope(rel)

        for match in import_pattern.finditer(stripped):
            imported = match.group(1)
            line = stripped.count("\n", 0, match.start()) + 1
            rule = ""
            if reflection_forbidden and imported.startswith(("java.lang.reflect.", "kotlin.reflect.")):
                rule = "runtime-reflection"
            elif (
                rel.startswith(source_prefix + "domain/backend/")
                or rel.startswith(source_prefix + "backend/declarations/")
            ) and imported.startswith("com.ghostlock.app.backend.backends."):
                rule = "backend-declaration-concrete-implementation"
            elif rel.startswith(source_prefix + "backend/backends/") and imported.startswith(
                (
                    "com.ghostlock.app.core.rootscript.",
                    "com.ghostlock.app.domain.finish.",
                    "com.ghostlock.app.domain.payload.",
                )
            ):
                rule = "backend-product-script-authority"
            elif layer == "domain" and (
                imported.startswith("android.")
                or imported.startswith("androidx.compose.")
                or imported.startswith(
                    (
                        "com.ghostlock.app.ui.",
                        "com.ghostlock.app.data.",
                        "com.ghostlock.app.backend.",
                        "com.ghostlock.app.core.",
                    ),
                )
            ):
                rule = "domain-outer-dependency"
            elif (
                rel.startswith(source_prefix + "data/backend/")
                and imported.startswith("com.ghostlock.app.backend.backends.")
            ):
                rule = "data-backend-concrete-implementation"
            elif (
                layer == "core"
                and imported.startswith("com.ghostlock.app.backend.backends.")
            ):
                rule = "core-concrete-backend-implementation"
            elif (
                rel.startswith(source_prefix + "backend/backends/")
                and imported.startswith("com.ghostlock.app.data.cve")
            ):
                rule = "backend-data-cve-reverse-dependency"
            elif layer in {"data", "backend"} and imported.startswith("com.ghostlock.app.ui."):
                rule = f"{layer}-ui-reverse-dependency"
            elif layer in {"data", "backend", "core"} and imported.startswith("androidx.compose."):
                rule = "non-ui-compose-dependency"
            elif layer == "ui" and imported.startswith(
                (
                    "com.ghostlock.app.data.",
                    "com.ghostlock.app.backend.",
                    "com.ghostlock.app.core.",
                ),
            ):
                rule = "ui-implementation-dependency"
            elif layer == "ui" and (
                imported == "android.content.SharedPreferences"
                or imported.startswith("rikka.shizuku.")
            ):
                rule = "ui-direct-platform-authority"
            if rule:
                violations.append(f"{rule}|{rel}:{line}|{imported}")

        if layer == "ui":
            for token, pattern in (
                ("getSharedPreferences", re.compile(r"\bgetSharedPreferences\s*\(")),
                ("Shizuku", re.compile(r"\bShizuku\.")),
            ):
                for match in pattern.finditer(stripped):
                    line = stripped.count("\n", 0, match.start()) + 1
                    violations.append(f"ui-direct-platform-authority|{rel}:{line}|{token}")

        if reflection_forbidden:
            for token, pattern in (
                ("Class.forName", re.compile(r"\bClass\s*\.\s*forName\s*\(")),
                ("getDeclaredMethod", re.compile(r"\.\s*getDeclaredMethod\s*\(")),
                ("getDeclaredField", re.compile(r"\.\s*getDeclaredField\s*\(")),
                ("getMethod", re.compile(r"\.\s*getMethod\s*\(")),
                ("getField", re.compile(r"\.\s*getField\s*\(")),
            ):
                for match in pattern.finditer(stripped):
                    line = stripped.count("\n", 0, match.start()) + 1
                    violations.append(f"runtime-reflection|{rel}:{line}|{token}")

        for match in re.finditer(
            r"\b(?:RunLogExportOpening|RunLogExportOpenResult|RunLogExportPort|"
            r"RunLogExportSession|RunLogExportCompletion|awaitWithin|awaitClosed|"
            r"RunLogSinkCompletion|awaitCompletion|completionMonitor|completionMonitorJob|"
            r"completion_monitor|"
            r"FormatWork|documentSequence|formatDrainActive|lastDequeuedSequence|"
            r"OPEN_WAIT_MS|TERMINAL_DRAIN_MS)\b",
            stripped,
        ):
            line = stripped.count("\n", 0, match.start()) + 1
            violations.append(
                f"superseded-run-log-topology|{rel}:{line}|{match.group(0)}"
            )

        if rel in {
            source_prefix + "backend/transport/AppRunHostClient.kt",
            source_prefix + "backend/transport/ShizukuRunHostClient.kt",
        }:
            for match in re.finditer(
                r"\b(?:RunLogSink|RunLogSinkFactory|AndroidRunLogSinkFactory)\b",
                stripped,
            ):
                line = stripped.count("\n", 0, match.start()) + 1
                violations.append(
                    f"runhost-client-log-sink-owner|{rel}:{line}|{match.group(0)}"
                )

        if rel.startswith(source_prefix + "backend/backends/"):
            for match in re.finditer(
                r"\b(?:RootScriptRenderer|PayloadScriptRenderer|RootScriptResultCodec|"
                r"PayloadDefinition|FinishDefinition|ProcessBuilder)\b",
                stripped,
            ):
                line = stripped.count("\n", 0, match.start()) + 1
                violations.append(
                    f"backend-product-script-authority|{rel}:{line}|{match.group(0)}"
                )

        if rel in {
            source_prefix + "domain/backend/BackendSpec.kt",
            source_prefix + "backend/declarations/BackendPlugin.kt",
        }:
            for match in re.finditer(
                r"\b(?:BackendCve\w*|Cve[0-9]\w*|A301\w*|DebugProbe\w*|Glued\w*|CustomLua\w*)\b",
                stripped,
            ):
                line = stripped.count("\n", 0, match.start()) + 1
                violations.append(
                    f"backend-declaration-concrete-implementation|{rel}:{line}|{match.group(0)}"
                )

    old_aidl_pattern = re.compile(
        r"\b(?:" + "|".join(re.escape(name) for name in OLD_AIDL_CONTRACTS) + r")\b"
    )
    kotlin_jni_patterns = (
        ("external-fun", re.compile(r"\bexternal\s+fun\b")),
        ("System.load", re.compile(r"\bSystem\s*\.\s*load(?:Library)?\s*\(")),
    )
    contract_paths = sorted((repo / "app/src").rglob("*.kt"))
    contract_paths += sorted((repo / "app/src/main/aidl").rglob("*.aidl"))
    contract_paths += [
        repo / "app/src/main/AndroidManifest.xml",
        repo / "app/proguard-rules.pro",
        repo / "app/build.gradle.kts",
    ]
    for path in contract_paths:
        if not path.is_file():
            continue
        rel = relative(path, repo)
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in old_aidl_pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            violations.append(
                f"old-aidl-transaction|{rel}:{line}|{match.group(0)}"
            )
        if path.suffix == ".kt":
            stripped = strip_kotlin(text)
            for token, pattern in kotlin_jni_patterns:
                for match in pattern.finditer(stripped):
                    line = stripped.count("\n", 0, match.start()) + 1
                    violations.append(
                        f"manual-jni-object-marshalling|{rel}:{line}|{token}"
                    )
    for name, rel in OLD_AIDL_CONTRACTS.items():
        if (repo / rel).exists():
            violations.append(f"old-aidl-transaction|{rel}:1|{name}")

    manual_jni_pattern = re.compile(
        r"\b(?:JNIEnv|JavaVM|jobject|jclass|jstring|jarray|NewGlobalRef|"
        r"DeleteGlobalRef|GetObjectClass|Call[A-Za-z]+Method|RegisterNatives|FindClass)\b"
    )
    native_root = repo / "app/src/main/cpp"
    for path in sorted(native_root.rglob("*")):
        if path.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
            continue
        rel = relative(path, repo)
        if rel.startswith(MANUAL_JNI_EXEMPT_PREFIXES):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for match in manual_jni_pattern.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            violations.append(
                f"manual-jni-object-marshalling|{rel}:{line}|{match.group(0)}"
            )

    return sorted(set(violations))


def file_action_rows(repo: Path) -> list[dict[str, object]]:
    paths = kotlin_source_paths(repo)
    paths += [repo / rel for rel in sorted(AUTOMATIC_LOG_TEST_TARGETS) if (repo / rel).is_file()]
    paths += sorted((repo / "app/src/main/aidl").rglob("*.aidl"))
    paths += sorted((repo / "app/src/main/res").rglob("*.xml"))
    paths += [repo / "app/src/main/AndroidManifest.xml"]
    rows: list[dict[str, object]] = []
    component_root = "app/src/main/kotlin/com/ghostlock/app/ui/component/"
    d7_move_targets = set(D7_FILE_MOVES.values())
    for path in sorted(set(paths)):
        rel = relative(path, repo)
        text = path.read_text(encoding="utf-8", errors="replace")
        action = "freeze"
        target = rel
        batch = "FROZEN"
        evidence = "existing-unlisted-path"
        if rel in D7_FILE_MOVES:
            action = "move-source-1to1"
            target = D7_FILE_MOVES[rel]
            batch = ADD_TARGETS[target]
            evidence = "stale-d7-source"
        elif rel in D7_FILE_DELETES:
            action = "delete"
            target = "ABSENT"
            batch = "D7.4"
            evidence = "stale-d7-deleted-source"
        elif rel in ADD_TARGETS:
            action = "move-target-1to1" if rel in d7_move_targets else "added-approved"
            batch = ADD_TARGETS[rel]
            evidence = "d7-one-to-one-target" if rel in d7_move_targets else "delivered-target"
        elif rel in DELETE_KOTLIN or rel == DELETE_AIDL:
            action = "delete"
            target = "ABSENT"
            batch = "D3a" if rel.endswith("IntentDispatcher.kt") else ("D1e" if rel.endswith("ui/BuildConfig.kt") else "D3b")
            evidence = "ksu-manager-delete-closure"
        elif rel.startswith(component_root):
            action = "preserve-path"
            batch = "D2" if any(part in "/" + rel for part in INPUTIZE_COMPONENT_PARTS) else "FROZEN"
            evidence = "component-71-file-preservation"
        elif contains_ksu_token(text):
            action = "modify-in-place"
            batch = "D3"
            evidence = "ksu-symbol-ledger"
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": owner_for(path, text),
            "action": action,
            "target": target,
            "batch": batch,
            "evidence": evidence,
            "source_state": "PRESENT",
        })
    current_paths = {str(row["path"]) for row in rows}
    for rel in sorted(DELETE_KOTLIN | {DELETE_AIDL}):
        if rel in current_paths:
            continue
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "ABSENT",
            "action": "delete",
            "target": "ABSENT",
            "batch": "D3a" if rel.endswith("IntentDispatcher.kt") else (
                "D1e" if rel.endswith("ui/BuildConfig.kt") else "D3b"
            ),
            "evidence": "deleted-manager-closure",
            "source_state": "ABSENT",
        })
    for rel, target in sorted(D7_FILE_MOVES.items()):
        if rel in current_paths:
            continue
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "ABSENT",
            "action": "move-source-1to1",
            "target": target,
            "batch": ADD_TARGETS[target],
            "evidence": "retired-d7-source",
            "source_state": "ABSENT",
        })
    for rel in sorted(D7_FILE_DELETES):
        if rel in current_paths:
            continue
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "ABSENT",
            "action": "delete",
            "target": "ABSENT",
            "batch": "D7.4",
            "evidence": "retired-dead-compatibility-layer",
            "source_state": "ABSENT",
        })
    for rel in sorted(R3_FILE_DELETES):
        if rel in current_paths:
            continue
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "ABSENT",
            "action": "delete",
            "target": "ABSENT",
            "batch": "R3",
            "evidence": "retired-r3-execution-spine",
            "source_state": "ABSENT",
        })
    for rel in sorted(R4_FILE_DELETES):
        if rel in current_paths:
            continue
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "ABSENT",
            "action": "delete",
            "target": "ABSENT",
            "batch": "R4",
            "evidence": "retired-r4-caller-zero-residue",
            "source_state": "ABSENT",
        })
    return rows


def textual_hits(path: Path) -> list[tuple[int, str, str]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    hits: list[tuple[int, str, str]] = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        if KSU_SYMBOL_PATTERN.search(line) is not None:
            hits.append((line_number, "ksu-symbol", line.strip()))
        for token in KSU_TOKENS:
            matched = KSU_IDENTIFIER_PATTERN.search(line) if token == "KSU" else None
            if matched is not None or (token != "KSU" and token in line):
                hits.append((line_number, token, line.strip()))
    return hits


def ksu_remove_rows(repo: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for rel in sorted(DELETE_KOTLIN | {DELETE_AIDL}):
        path = repo / rel
        exists = path.is_file()
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": (
                owner_for(path, path.read_text(encoding="utf-8", errors="replace"))
                if exists else "ABSENT"
            ),
            "action": "DELETE",
            "target": "ABSENT",
            "batch": "D3a" if rel.endswith("IntentDispatcher.kt") else ("D1e" if rel.endswith("ui/BuildConfig.kt") else "D3b"),
            "evidence": "manager-delete-closure" if exists else "deleted-manager-closure",
            "identity_state": "PRESENT" if exists else "ABSENT",
        })
    keep_paths = KEEP_KOTLIN | KEEP_NATIVE
    for path in sorted((repo / "app/src/main").rglob("*")):
        if not path.is_file():
            continue
        if path.suffix not in {".kt", ".aidl", ".xml"}:
            continue
        rel = relative(path, repo)
        if rel in DELETE_KOTLIN or rel == DELETE_AIDL or rel in keep_paths:
            continue
        try:
            hits = textual_hits(path)
        except (UnicodeDecodeError, OSError):
            continue
        for line, token, literal in hits:
            action = "HISTORY_REWRITE" if rel.endswith("BackendStatusCard.kt") and token == "KernelSU" else "REMOVE_OR_RENAME"
            rows.append({
                "path": rel,
                "symbol_resource": f"line:{line}:{token}",
                "current_owner": owner_for(path, path.read_text(encoding="utf-8", errors="replace")),
                "action": action,
                "target": rel,
                "batch": "D2" if "/component/" in rel else "D3",
                "evidence": literal,
                "identity_state": "PRESENT",
            })
    manifest = repo / "app/src/main/AndroidManifest.xml"
    manifest_text = manifest.read_text(encoding="utf-8")
    for line_number, line in enumerate(manifest_text.splitlines(), start=1):
        if "Theme.KernelSU" in line or "KernelSU 管理器" in line:
            literal = line.strip()
            rows.append({
                "path": relative(manifest, repo),
                "symbol_resource": f"line:{line_number}:branding",
                "current_owner": "AndroidManifest",
                "action": "RENAME",
                "target": relative(manifest, repo),
                "batch": "D3e",
                "evidence": literal,
                "identity_state": "PRESENT",
            })
    return rows


def ksu_keep_rows(repo: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for rel in sorted(KEEP_KOTLIN):
        path = repo / rel
        hits = textual_hits(path)
        if not hits:
            hits = [(1, "execution-contract", "file")]
        for line, token, literal in hits:
            rows.append({
                "path": rel,
                "symbol_resource": f"line:{line}:{token}",
                "current_owner": owner_for(path, path.read_text(encoding="utf-8", errors="replace")),
                "action": "KEEP_LITERAL",
                "target": rel,
                "batch": ADD_TARGETS.get(rel, "FROZEN-D1-D6"),
                "evidence": literal,
                "identity_state": "PRESENT",
            })
    for rel in sorted(KEEP_NATIVE):
        path = repo / rel
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "native-or-asset",
            "action": "KEEP_PRESENCE",
            "target": rel,
            "batch": "FROZEN-NATIVE",
            "evidence": "native-handoff-loader-evidence",
            "identity_state": "PRESENT",
        })
    manifest = repo / "app/src/main/AndroidManifest.xml"
    for package_name in ("me.weishu.kernelsu.pr", "me.weishu.kernelsu"):
        literal = f'<package android:name="{package_name}" />'
        rows.append({
            "path": relative(manifest, repo),
            "symbol_resource": "queries/package:" + package_name,
            "current_owner": "AndroidManifest",
            "action": "KEEP_LITERAL",
            "target": relative(manifest, repo),
            "batch": "FROZEN-D1-D6",
            "evidence": literal,
            "identity_state": "PRESENT",
        })
    for rel in sorted(KEEP_TESTS):
        path = repo / rel
        rows.append({
            "path": rel,
            "symbol_resource": "file",
            "current_owner": "test",
            "action": "KEEP_TEST",
            "target": rel,
            "batch": "FROZEN-TEST",
            "evidence": "guard-preflight-handoff-characterization",
            "identity_state": "PRESENT",
        })
    return rows


def surface_rows() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []

    def add(group: str, surface: str, state: str, mode: str, theme: str, layout: str, action: str) -> None:
        member_id = "_".join((group.lower(), surface, state, mode, theme, layout)).replace("-", "_")
        records = 3 if group in {"M1", "M2"} else 1
        rows.append({
            "member_id": member_id,
            "group": group,
            "surface": surface,
            "state": state,
            "mode": mode,
            "theme": theme,
            "layout": layout,
            "action": action,
            "screenshot": f"{group.lower()}/{member_id}.png" if action == "capture" else "",
            "semantics": f"{group.lower()}/{member_id}.json" if action == "capture" else "",
            "screenshot_sha256": "",
            "semantics_sha256": "",
            "semantics_records": records if action == "capture" else 1,
            "panel_count": 3 if group == "M5c" else 0,
            "mask": "",
            "evidence": "production-composable",
        })

    for surface in ("home", "backend", "run_idle", "settings"):
        for mode in ("material", "miuix"):
            for theme in ("light", "dark"):
                add("M1", surface, "normal", mode, theme, "narrow", "capture")
    for surface in ("about", "color_palette", "finish", "finish_settings"):
        for mode in ("material", "miuix"):
            for theme in ("light", "dark"):
                add("M2", surface, "normal", mode, theme, "narrow", "capture")
    for surface in ("home", "backend", "run_idle", "settings"):
        for mode in ("material", "miuix"):
            for theme in ("light", "dark"):
                add("M3", surface, "normal", mode, theme, "wide", "capture")
    for surface in ("config_43499", "config_43284", "config_debug"):
        for mode in ("material", "miuix"):
            for theme in ("light", "dark"):
                add("M4", surface, "normal", mode, theme, "narrow", "capture")
    for state in ("running", "blocked", "handoff", "done", "long_log"):
        for mode in ("material", "miuix"):
            for theme in ("light", "dark"):
                add("M5a", "run", state, mode, theme, "narrow", "deferred-D5")
    for mode in ("material", "miuix"):
        for theme in ("light", "dark"):
            add("M5b", "confirm_dialog", "normal", mode, theme, "narrow", "capture")
            add("M5c", "component_gallery", "disabled_empty_oversize", mode, theme, "narrow", "capture")
    return rows


FILE_ACTION_FIELDS = (
    "path", "symbol_resource", "current_owner", "action", "target", "batch", "evidence", "source_state",
)
COMPONENT_FIELDS = (
    "row_type", "path", "kind", "owner_fqn", "name", "receiver_type", "type_parameter_arity",
    "normalized_parameter_types", "api_key", "old_signature_sha", "action", "target_api_key",
    "target_signature", "parameter_mapping", "default_mapping", "caller_manifest", "batch", "evidence",
    "source_line", "declared_type", "mutability",
)
KSU_FIELDS = (
    "path", "symbol_resource", "current_owner", "action", "target", "batch", "evidence", "identity_state",
)
SURFACE_FIELDS = (
    "member_id", "group", "surface", "state", "mode", "theme", "layout", "action", "screenshot",
    "semantics", "screenshot_sha256", "semantics_sha256", "semantics_records", "panel_count", "mask", "evidence",
)


def write_inventory(repo: Path, output: Path) -> None:
    surface = surface_rows()
    existing_surface = output / "ui-surface-matrix-v1.tsv"
    if existing_surface.is_file():
        existing_hashes = {
            row["member_id"]: (row.get("screenshot_sha256", ""), row.get("semantics_sha256", ""))
            for row in read_tsv(existing_surface)
        }
        for row in surface:
            screenshot_hash, semantics_hash = existing_hashes.get(str(row["member_id"]), ("", ""))
            if re.fullmatch(r"[0-9a-f]{64}", screenshot_hash):
                row["screenshot_sha256"] = screenshot_hash
            if re.fullmatch(r"[0-9a-f]{64}", semantics_hash):
                row["semantics_sha256"] = semantics_hash
    data = {
        "kotlin-file-actions-v1.tsv": tsv_bytes(FILE_ACTION_FIELDS, file_action_rows(repo)),
        "component-api-v1.tsv": tsv_bytes(COMPONENT_FIELDS, component_rows(repo)),
        "ksu-ui-remove-v1.tsv": tsv_bytes(KSU_FIELDS, ksu_remove_rows(repo)),
        "ksu-exec-keep-v1.tsv": tsv_bytes(KSU_FIELDS, ksu_keep_rows(repo)),
        "ui-surface-matrix-v1.tsv": tsv_bytes(SURFACE_FIELDS, surface),
    }
    for name, payload in data.items():
        atomic_write(output / name, payload)


def verify_inventory(repo: Path, fixtures: Path, section: str = "all") -> dict[str, int]:
    missing = [name for name in TSV_NAMES if not (fixtures / name).is_file()]
    if missing:
        raise ValueError("missing manifests: " + ",".join(missing))
    result: dict[str, int] = {}
    if section in {"all", "architecture"}:
        violations = architecture_violations(repo)
        if violations:
            rule_counts = {
                rule: sum(item.startswith(rule + "|") for item in violations)
                for rule in ARCHITECTURE_RULES
            }
            summary = " ".join(
                rule.upper().replace("-", "_") + "=" + str(rule_counts[rule])
                for rule in ARCHITECTURE_RULES
            )
            raise ValueError("architecture violations " + summary + ":\n" + "\n".join(violations))
        result["ARCH_VIOLATIONS"] = 0
        for rule in ARCHITECTURE_RULES:
            result[rule.upper().replace("-", "_")] = 0
    if section in {"all", "inventory"}:
        rows = read_tsv(fixtures / "kotlin-file-actions-v1.tsv")
        keys = [row["path"] for row in rows]
        if len(keys) != len(set(keys)):
            raise ValueError("duplicate file-action path")
        current = {relative(path, repo) for path in kotlin_source_paths(repo)}
        covered = {row["path"] for row in rows if row["source_state"] == "PRESENT" and row["path"].endswith(".kt")}
        unclassified = current - covered
        if unclassified:
            raise ValueError("unclassified Kotlin: " + ",".join(sorted(unclassified)))
        by_path = {row["path"]: row for row in rows}
        for source, target in D7_FILE_MOVES.items():
            source_row = by_path.get(source)
            target_row = by_path.get(target)
            if (repo / source).exists():
                raise ValueError("stale D7 move source: " + source)
            if source_row is None or source_row.get("source_state") != "ABSENT":
                raise ValueError("missing retired D7 source row: " + source)
            if source_row.get("action") != "move-source-1to1" or source_row.get("target") != target:
                raise ValueError("invalid D7 source mapping: " + source)
            if target_row is None or target_row.get("source_state") != "PRESENT":
                raise ValueError("missing D7 move target: " + target)
            if target_row.get("action") != "move-target-1to1":
                raise ValueError("invalid D7 target action: " + target)
        for deleted in D7_FILE_DELETES:
            deleted_row = by_path.get(deleted)
            if (repo / deleted).exists():
                raise ValueError("stale D7 deleted source: " + deleted)
            if (
                deleted_row is None
                or deleted_row.get("source_state") != "ABSENT"
                or deleted_row.get("action") != "delete"
                or deleted_row.get("target") != "ABSENT"
            ):
                raise ValueError("invalid D7 delete row: " + deleted)
        for deleted in R3_FILE_DELETES:
            deleted_row = by_path.get(deleted)
            if (repo / deleted).exists():
                raise ValueError("stale R3 execution-spine source: " + deleted)
            if (
                deleted_row is None
                or deleted_row.get("source_state") != "ABSENT"
                or deleted_row.get("action") != "delete"
                or deleted_row.get("target") != "ABSENT"
                or deleted_row.get("batch") != "R3"
            ):
                raise ValueError("invalid R3 delete row: " + deleted)
        for deleted in R4_FILE_DELETES:
            deleted_row = by_path.get(deleted)
            if (repo / deleted).exists():
                raise ValueError("stale R4 residue: " + deleted)
            if (
                deleted_row is None
                or deleted_row.get("source_state") != "ABSENT"
                or deleted_row.get("action") != "delete"
                or deleted_row.get("target") != "ABSENT"
                or deleted_row.get("batch") != "R4"
            ):
                raise ValueError("invalid R4 delete row: " + deleted)
        result["UNCLASSIFIED"] = 0
        result["FILE_ACTION_ROWS"] = len(rows)
    if section in {"all", "component"}:
        rows = read_tsv(fixtures / "component-api-v1.tsv")
        files = [
            row for row in rows
            if row["row_type"] == "file" and row["action"] != "rename-source-1to1"
        ]
        apis = [row for row in rows if row["row_type"] == "api"]
        generated = component_rows(repo)
        generated_files = {
            row["path"] for row in generated
            if row["row_type"] == "file" and row["action"] != "rename-source-1to1"
        }
        actual_files = {row["path"] for row in files}
        if len(files) != len(actual_files) or actual_files != generated_files:
            raise ValueError(
                "component file drift missing=" + str(len(generated_files - actual_files)) +
                " extra=" + str(len(actual_files - generated_files))
            )
        keys = [row["api_key"] for row in apis]
        if len(keys) != len(set(keys)):
            duplicates = sorted({key for key in keys if keys.count(key) > 1})
            raise ValueError("API key collisions: " + ",".join(duplicates[:10]))
        expected = {(row["api_key"], row["old_signature_sha"]) for row in generated if row["row_type"] == "api"}
        actual = {(row["api_key"], row["old_signature_sha"]) for row in apis}
        if actual != expected:
            raise ValueError(f"component API drift missing={len(expected-actual)} extra={len(actual-expected)}")
        source_file = next(
            (row for row in rows if row.get("api_key") == "file|" + D7_COMPONENT_SOURCE_PATH),
            None,
        )
        source_api = next(
            (row for row in rows if row.get("api_key") == D7_COMPONENT_SOURCE_API_KEY),
            None,
        )
        if (
            source_file is None
            or source_file.get("action") != "rename-source-1to1"
            or source_file.get("target_api_key") != "file|" + D7_COMPONENT_TARGET_PATH
        ):
            raise ValueError("D7 component file mapping missing")
        if (
            source_api is None
            or source_api.get("action") != "rename-source-1to1"
            or source_api.get("target_api_key") != D7_COMPONENT_TARGET_API_KEY
        ):
            raise ValueError("D7 component API mapping missing")
        result["COMPONENT_FILES"] = len(files)
        result["COMPONENT_API_ROWS"] = len(apis)
        result["COMPONENT_UNCLASSIFIED"] = 0
        result["API_KEY_COLLISIONS"] = 0
        result["API_MAPPING_INVALID"] = 0
    if section in {"all", "ksu"}:
        remove = read_tsv(fixtures / "ksu-ui-remove-v1.tsv")
        keep = read_tsv(fixtures / "ksu-exec-keep-v1.tsv")
        delete_paths = {row["path"] for row in remove if row["action"] == "DELETE"}
        expected_delete = DELETE_KOTLIN | {DELETE_AIDL}
        if delete_paths != expected_delete:
            raise ValueError(f"delete closure drift missing={expected_delete-delete_paths} extra={delete_paths-expected_delete}")
        residue = [row for row in remove if row["action"] != "DELETE"]
        if residue:
            preview = ",".join(row["path"] + ":" + row["symbol_resource"] for row in residue[:10])
            raise ValueError("KSU UI residue: " + preview)
        keep_files = {row["path"] for row in keep}
        required = KEEP_KOTLIN | KEEP_NATIVE | KEEP_TESTS | {"app/src/main/AndroidManifest.xml"}
        if not required.issubset(keep_files):
            raise ValueError("KEEP paths missing: " + ",".join(sorted(required - keep_files)))
        result["REMOVE_ROWS"] = len(remove)
        result["KEEP_ROWS"] = len(keep)
        result["REMOVE_RESIDUE"] = 0
        result["KEEP_DRIFT"] = 0
    if section in {"all", "surface"}:
        rows = read_tsv(fixtures / "ui-surface-matrix-v1.tsv")
        active = [row for row in rows if row["action"] == "capture"]
        deferred = [row for row in rows if row["action"] == "deferred-D5"]
        if (len(rows), len(active), len(deferred)) != (88, 68, 20):
            raise ValueError(f"surface counts={len(rows)}/{len(active)}/{len(deferred)}")
        semantics_records = sum(int(row["semantics_records"]) for row in active)
        if semantics_records != 132:
            raise ValueError(f"semantics records={semantics_records}")
        active_hash_states = {
            bool(row.get("screenshot_sha256")) and bool(row.get("semantics_sha256"))
            for row in active
        }
        if len(active_hash_states) != 1:
            raise ValueError("surface hashes are partially populated")
        for row in active:
            for field in ("screenshot_sha256", "semantics_sha256"):
                value = row.get(field, "")
                if value and re.fullmatch(r"[0-9a-f]{64}", value) is None:
                    raise ValueError(f"invalid {field} for {row['member_id']}")
        for row in deferred:
            if row.get("screenshot_sha256") or row.get("semantics_sha256"):
                raise ValueError(f"deferred surface has hashes: {row['member_id']}")
        result["SURFACES"] = len(rows)
        result["D0_CAPTURE"] = len(active)
        result["DEFERRED_D5"] = len(deferred)
        result["SEMANTICS_RECORDS"] = semantics_records
    return result


def sdk_dir(repo: Path) -> Path:
    properties = repo / "local.properties"
    for line in properties.read_text(encoding="utf-8").splitlines():
        if line.startswith("sdk.dir="):
            return Path(line.split("=", 1)[1].replace("\\:", ":").replace("\\\\", "\\"))
    raise ValueError("sdk.dir missing from local.properties")


def run(
    command: Sequence[str],
    *,
    check: bool = True,
    capture: bool = True,
    timeout: float | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, check=check, text=True, capture_output=capture, timeout=timeout)


def adb_path(repo: Path) -> Path:
    return sdk_dir(repo) / "platform-tools/adb"


def avd_static_identity(repo: Path, avd_name: str, snapshot: str) -> dict[str, object]:
    avd = Path.home() / ".android/avd" / f"{avd_name}.avd"
    config = avd / "config.ini"
    snapshot_dir = avd / "snapshots" / snapshot
    files = sorted(path for path in snapshot_dir.iterdir() if path.is_file())
    manifest = "".join(f"{path.name}\t{path.stat().st_size}\t{sha256_file(path)}\n" for path in files).encode()
    config_values = {}
    for line in config.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            config_values[key] = value
    build_prop = sdk_dir(repo) / config_values["image.sysdir.1"] / "build.prop"
    return {
        "config_sha": sha256_file(config),
        "snapshot_files": len(files),
        "snapshot_bytes": sum(path.stat().st_size for path in files),
        "snapshot_manifest_sha": sha256_bytes(manifest),
        "build_prop_sha": sha256_file(build_prop),
    }


EXPECTED_AVD_IDENTITY = {
    "config_sha": "df8ee6515740419d19827e3875887ed9251a1bb36e0b9470bbf034f8802f5d53",
    "snapshot_files": 5,
    "snapshot_bytes": 3063024640,
    "snapshot_manifest_sha": "9a474d743d26fee0a1d5c9846cb887c5980d1a27f3302131268ecdaf94e26c02",
    "build_prop_sha": "5b6526736e4db77700c5b21ff602858d4b42ac355ca0b5101f6695467a4a168a",
}


def process_field(pid: int, field: str) -> str:
    completed = run(["ps", "-p", str(pid), "-o", f"{field}="], check=False)
    return completed.stdout.strip() if completed.returncode == 0 else ""


def process_matches(state: dict[str, object]) -> bool:
    pid = int(state["pid"])
    command = process_field(pid, "command")
    start = process_field(pid, "lstart")
    expected_start = str(state.get("pid_start", ""))
    expected_exe = str(state.get("exe", ""))
    return bool(command) and expected_exe in command and start == expected_start


def wait_for_boot(adb: Path, serial: str, timeout_seconds: float = 180) -> None:
    run([str(adb), "-s", serial, "wait-for-device"], timeout=timeout_seconds)
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        completed = run(
            [str(adb), "-s", serial, "shell", "getprop", "sys.boot_completed"],
            check=False,
            timeout=15,
        )
        if completed.returncode == 0 and completed.stdout.strip() == "1":
            return
        time.sleep(1)
    raise TimeoutError(f"boot timeout for {serial}")


def adb_text(adb: Path, serial: str, *arguments: str, check: bool = True) -> str:
    completed = run([str(adb), "-s", serial, *arguments], check=check, timeout=30)
    return completed.stdout.strip()


def wm_override(output: str, kind: str) -> str | None:
    match = re.search(rf"Override {re.escape(kind)}:\s*([^\s]+)", output)
    return match.group(1) if match else None


def read_avd_runtime(adb: Path, serial: str) -> dict[str, object]:
    size = adb_text(adb, serial, "shell", "wm", "size")
    density = adb_text(adb, serial, "shell", "wm", "density")
    return {
        "adbd_uid": adb_text(adb, serial, "shell", "id", "-u"),
        "locale": adb_text(adb, serial, "shell", "getprop", "persist.sys.locale"),
        "size_raw": size,
        "size_override": wm_override(size, "size"),
        "density_raw": density,
        "density_override": wm_override(density, "density"),
        "font_scale": adb_text(adb, serial, "shell", "settings", "get", "system", "font_scale"),
        "window_animation_scale": adb_text(adb, serial, "shell", "settings", "get", "global", "window_animation_scale"),
        "transition_animation_scale": adb_text(adb, serial, "shell", "settings", "get", "global", "transition_animation_scale"),
        "animator_duration_scale": adb_text(adb, serial, "shell", "settings", "get", "global", "animator_duration_scale"),
        "accelerometer_rotation": adb_text(adb, serial, "shell", "settings", "get", "system", "accelerometer_rotation"),
        "user_rotation": adb_text(adb, serial, "shell", "settings", "get", "system", "user_rotation"),
    }


def put_setting(adb: Path, serial: str, namespace: str, name: str, value: str) -> None:
    adb_text(adb, serial, "shell", "settings", "put", namespace, name, value)


def restore_setting(adb: Path, serial: str, namespace: str, name: str, value: object) -> None:
    text = str(value)
    if text in {"", "null", "None"}:
        adb_text(adb, serial, "shell", "settings", "delete", namespace, name)
    else:
        put_setting(adb, serial, namespace, name, text)


def restart_framework(adb: Path, serial: str) -> None:
    adb_text(adb, serial, "shell", "stop")
    adb_text(adb, serial, "shell", "start")
    wait_for_boot(adb, serial)


def assert_runtime_fields(actual: dict[str, object], expected: dict[str, object], stage: str) -> None:
    for field, expected_value in expected.items():
        if str(actual.get(field)) != str(expected_value):
            raise RuntimeError(
                f"{stage} field mismatch: {field} expected={expected_value!r} actual={actual.get(field)!r}",
            )


def update_transaction_state(path: Path, state: dict[str, object], stage: str) -> None:
    state["stage"] = stage
    atomic_write(path, json.dumps(state, indent=2, sort_keys=True).encode() + b"\n")


def apply_capture_environment(adb: Path, serial: str, size: str, density: str) -> dict[str, object]:
    adb_text(adb, serial, "shell", "wm", "size", size)
    adb_text(adb, serial, "shell", "wm", "density", density)
    put_setting(adb, serial, "system", "font_scale", "1.0")
    put_setting(adb, serial, "global", "window_animation_scale", "0")
    put_setting(adb, serial, "global", "transition_animation_scale", "0")
    put_setting(adb, serial, "global", "animator_duration_scale", "0")
    put_setting(adb, serial, "system", "accelerometer_rotation", "0")
    put_setting(adb, serial, "system", "user_rotation", "0")
    actual = read_avd_runtime(adb, serial)
    assert_runtime_fields(
        actual,
        {
            "adbd_uid": "0",
            "locale": "zh-CN",
            "size_override": size,
            "density_override": density,
            "font_scale": "1.0",
            "window_animation_scale": "0.0",
            "transition_animation_scale": "0.0",
            "animator_duration_scale": "0.0",
            "accelerometer_rotation": "0",
            "user_rotation": "0",
        },
        f"active-{size}-{density}",
    )
    return actual


def restore_avd_runtime(adb: Path, serial: str, before: dict[str, object]) -> list[str]:
    errors: list[str] = []

    def attempt(label: str, action: object) -> None:
        try:
            action()  # type: ignore[operator]
        except BaseException as error:  # noqa: BLE001 - cleanup must continue through every field.
            errors.append(f"{label}:{type(error).__name__}:{error}")

    attempt("locale", lambda: adb_text(adb, serial, "shell", "setprop", "persist.sys.locale", str(before["locale"])))
    attempt("framework", lambda: restart_framework(adb, serial))
    size_override = before.get("size_override")
    density_override = before.get("density_override")
    attempt(
        "size",
        lambda: adb_text(adb, serial, "shell", "wm", "size", str(size_override))
        if size_override else adb_text(adb, serial, "shell", "wm", "size", "reset"),
    )
    attempt(
        "density",
        lambda: adb_text(adb, serial, "shell", "wm", "density", str(density_override))
        if density_override else adb_text(adb, serial, "shell", "wm", "density", "reset"),
    )
    for namespace, name in (
        ("system", "font_scale"),
        ("global", "window_animation_scale"),
        ("global", "transition_animation_scale"),
        ("global", "animator_duration_scale"),
        ("system", "accelerometer_rotation"),
        ("system", "user_rotation"),
    ):
        attempt(name, lambda ns=namespace, key=name: restore_setting(adb, serial, ns, key, before[key]))
    if str(before.get("adbd_uid")) != "0":
        attempt("unroot", lambda: run([str(adb), "-s", serial, "unroot"], timeout=30))
        attempt("unroot-wait", lambda: wait_for_boot(adb, serial))
    try:
        restored = read_avd_runtime(adb, serial)
        for field in (
            "adbd_uid", "locale", "size_override", "density_override", "font_scale",
            "window_animation_scale", "transition_animation_scale", "animator_duration_scale",
            "accelerometer_rotation", "user_rotation",
        ):
            if str(restored.get(field)) != str(before.get(field)):
                errors.append(f"verify-{field}:expected={before.get(field)!r}:actual={restored.get(field)!r}")
    except BaseException as error:  # noqa: BLE001 - process teardown still must run.
        errors.append(f"verify-runtime:{type(error).__name__}:{error}")
    return errors


def png_dimensions(payload: bytes) -> tuple[int, int]:
    if len(payload) < 24 or payload[:8] != b"\x89PNG\r\n\x1a\n" or payload[12:16] != b"IHDR":
        raise ValueError("invalid PNG header")
    return int.from_bytes(payload[16:20], "big"), int.from_bytes(payload[20:24], "big")


def validate_capture_files(output: Path, matrix: Path) -> tuple[int, int, int, int]:
    active = [row for row in read_tsv(matrix) if row["action"] == "capture"]
    screenshot_count = 0
    semantics_count = 0
    record_count = 0
    panel_count = 0
    expected_files: set[str] = set()
    for row in active:
        expected_files.update((row["screenshot"], row["semantics"]))
        screenshot_payload = (output / row["screenshot"]).read_bytes()
        expected_size = (1600, 2560) if row["layout"] == "wide" else (1080, 2400)
        if png_dimensions(screenshot_payload) != expected_size:
            raise ValueError(f"screenshot bounds mismatch: {row['member_id']}")
        screenshot_count += 1
        document = json.loads((output / row["semantics"]).read_text(encoding="utf-8"))
        if document.get("member_id") != row["member_id"]:
            raise ValueError(f"semantics member mismatch: {row['member_id']}")
        records = document.get("records")
        if not isinstance(records, list):
            raise ValueError(f"semantics records missing: {row['member_id']}")
        expected_names = ["normal", "rotation", "back"] if row["group"] in {"M1", "M2"} else ["normal"]
        if [record.get("name") for record in records] != expected_names:
            raise ValueError(f"semantics record names mismatch: {row['member_id']}")
        if len(records) != int(row["semantics_records"]):
            raise ValueError(f"semantics record count mismatch: {row['member_id']}")
        for record in records:
            if not str(record.get("tree", "")).strip():
                raise ValueError(f"empty semantics tree: {row['member_id']}")
        if row["group"] == "M5c":
            panels = records[0].get("panels", [])
            if [panel.get("name") for panel in panels] != ["disabled", "empty", "oversize"]:
                raise ValueError(f"M5c panel mismatch: {row['member_id']}")
            if not all(panel.get("present") is True for panel in panels):
                raise ValueError(f"M5c panel assertion failed: {row['member_id']}")
            panel_count += len(panels)
        semantics_count += 1
        record_count += len(records)
    actual_files = {
        path.relative_to(output).as_posix()
        for group in ("m1", "m2", "m3", "m4", "m5b", "m5c")
        for path in (output / group).rglob("*") if path.is_file()
    }
    if actual_files != expected_files:
        raise ValueError(
            f"capture members mismatch missing={len(expected_files-actual_files)} extra={len(actual_files-expected_files)}",
        )
    return screenshot_count, semantics_count, record_count, panel_count


def capture(args: argparse.Namespace) -> None:
    repo = Path(args.repo).resolve()
    matrix = Path(args.matrix).resolve()
    fixtures = matrix.parent
    try:
        actual_identity = avd_static_identity(repo, args.avd, args.snapshot)
    except (FileNotFoundError, KeyError) as error:
        print(
            "D0_CAPTURE=FAIL IDENTITY_MATCH=0 FIELD=snapshot-static "
            f"EXPECTED=present ACTUAL={type(error).__name__}:{error}",
        )
        raise SystemExit(2) from error
    if actual_identity != EXPECTED_AVD_IDENTITY:
        for field, expected in EXPECTED_AVD_IDENTITY.items():
            if actual_identity[field] != expected:
                print(
                    f"D0_CAPTURE=FAIL IDENTITY_MATCH=0 FIELD={field} "
                    f"EXPECTED={expected} ACTUAL={actual_identity[field]}",
                )
                raise SystemExit(2)
    verify_inventory(repo, fixtures, "surface")
    process_list = run(["ps", "-axo", "command="], check=True).stdout.splitlines()
    conflicts = [
        line for line in process_list
        if f"-avd {args.avd}" in line and ("qemu-system-" in line or "/emulator " in line)
    ]
    if conflicts:
        print(f"D0_CAPTURE=FAIL FIELD=avd-exclusive ACTUAL={len(conflicts)}")
        raise SystemExit(2)

    sdk = sdk_dir(repo)
    emulator = (sdk / "emulator/emulator").resolve()
    adb = adb_path(repo).resolve()
    serial = f"emulator-{args.port}"
    output = Path(args.out).resolve()
    output.mkdir(parents=True, exist_ok=True)
    for group in ("m1", "m2", "m3", "m4", "m5b", "m5c"):
        shutil.rmtree(output / group, ignore_errors=True)
    state_path = output / "avd-before.json"
    command = [
        str(emulator), "-avd", args.avd, "-snapshot", args.snapshot,
        "-force-snapshot-load", "-no-snapshot-save", "-no-snapshot-update-time",
        "-read-only", "-no-window", "-no-audio", "-no-boot-anim",
        "-gpu", "swiftshader", "-port", str(args.port),
    ]
    emulator_log_path = output / "emulator.log"
    emulator_log = emulator_log_path.open("wb")
    process = subprocess.Popen(command, stdout=emulator_log, stderr=subprocess.STDOUT)
    state: dict[str, object] = {
        "status": "incomplete",
        "avd": args.avd,
        "snapshot": args.snapshot,
        "serial": serial,
        "ports": [args.port, args.port + 1],
        "pid": process.pid,
        "pid_start": process_field(process.pid, "lstart"),
        "exe": str(emulator),
        "argv": command,
        "stage": "spawned",
        "before": None,
        "hashes": actual_identity,
        "repo": str(repo),
        "adb": str(adb),
        "emulator_log": str(emulator_log_path),
    }
    update_transaction_state(state_path, state, "spawned")
    abort_signal: list[int] = []
    previous_handlers: dict[int, object] = {}

    def request_abort(signum: int, _frame: object) -> None:
        if not abort_signal:
            abort_signal.append(signum)

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        previous_handlers[signum] = signal.getsignal(signum)
        signal.signal(signum, request_abort)

    def check_abort() -> None:
        if abort_signal:
            raise RuntimeError(f"capture aborted by signal {abort_signal[0]}")

    failure: BaseException | None = None
    restore_errors: list[str] = []
    capture_counts: tuple[int, int, int, int] | None = None
    try:
        wait_for_boot(adb, serial)
        update_transaction_state(state_path, state, "booted")
        check_abort()
        avd_output = adb_text(adb, serial, "emu", "avd", "name")
        avd_name = avd_output.splitlines()[0] if avd_output else ""
        fingerprint = adb_text(adb, serial, "shell", "getprop", "ro.build.fingerprint")
        expected_fingerprint = "google/sdk_gphone64_arm64/emu64a:13/TE1A.240213.009/12342917:userdebug/dev-keys"
        if avd_name != args.avd:
            raise RuntimeError(f"AVD name mismatch: expected={args.avd} actual={avd_name}")
        if fingerprint != expected_fingerprint:
            raise RuntimeError(f"fingerprint mismatch: expected={expected_fingerprint} actual={fingerprint}")

        before = read_avd_runtime(adb, serial)
        state["before"] = before
        update_transaction_state(state_path, state, "before-recorded")
        root_result = run([str(adb), "-s", serial, "root"], check=False, timeout=30)
        if root_result.returncode != 0:
            raise RuntimeError("adb root failed: " + root_result.stdout + root_result.stderr)
        wait_for_boot(adb, serial)
        assert_runtime_fields(read_avd_runtime(adb, serial), {"adbd_uid": "0"}, "root")
        update_transaction_state(state_path, state, "rooted")
        adb_text(adb, serial, "shell", "setprop", "persist.sys.locale", "zh-CN")
        restart_framework(adb, serial)
        update_transaction_state(state_path, state, "locale-active")

        installed = False
        expected_suite_counts = {"narrow": (52, 116), "wide": (16, 16)}
        for size, density, suite in (("1080x2400", "420", "narrow"), ("1600x2560", "320", "wide")):
            check_abort()
            active_runtime = apply_capture_environment(adb, serial, size, density)
            state[f"active_{suite}"] = active_runtime
            update_transaction_state(state_path, state, f"active-{suite}")
            if not installed:
                for apk in (args.debug_apk, args.test_apk):
                    completed = run(
                        [str(adb), "-s", serial, "install", "-r", str(Path(apk).resolve())],
                        check=False,
                        timeout=180,
                    )
                    if completed.returncode != 0 or "Success" not in completed.stdout:
                        raise RuntimeError("install failed: " + completed.stdout + completed.stderr)
                installed = True
                update_transaction_state(state_path, state, "installed")
            instrument = [
                str(adb), "-s", serial, "shell", "am", "instrument", "-w",
                "-e", "class", args.test_class,
                "-e", "suite", suite,
                "com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner",
            ]
            completed = run(instrument, check=False, timeout=900)
            print(completed.stdout, end="")
            expected_screenshots, expected_records = expected_suite_counts[suite]
            fixture_line = (
                f"D0_CAPTURE_FIXTURE=PASS SUITE={suite} SCREENSHOTS={expected_screenshots} "
                f"SEMANTICS_FILES={expected_screenshots} SEMANTICS_RECORDS={expected_records}"
            )
            if completed.returncode != 0 or "OK (1 test)" not in completed.stdout or fixture_line not in completed.stdout:
                raise RuntimeError(f"instrumentation failed for {suite}: {completed.stdout}{completed.stderr}")
            update_transaction_state(state_path, state, f"captured-{suite}")

        active = [row for row in read_tsv(matrix) if row["action"] == "capture"]
        for row in active:
            for key in ("screenshot", "semantics"):
                remote = "files/d0-capture/" + row[key]
                destination = output / row[key]
                completed = subprocess.run(
                    [str(adb), "-s", serial, "exec-out", "run-as", "com.ghostlock.app", "cat", remote],
                    check=False,
                    capture_output=True,
                    timeout=60,
                )
                if completed.returncode != 0 or not completed.stdout:
                    raise RuntimeError(f"missing capture {remote}: {completed.stderr.decode(errors='replace')}")
                atomic_write(destination, completed.stdout)
        capture_counts = validate_capture_files(output, matrix)
        if capture_counts != (68, 68, 132, 12):
            raise RuntimeError(f"capture count mismatch: {capture_counts}")
        update_transaction_state(state_path, state, "host-validated")
    except BaseException as error:  # noqa: BLE001 - all paths must enter verified restore.
        failure = error
    finally:
        before_value = state.get("before")
        if isinstance(before_value, dict):
            restore_errors.extend(restore_avd_runtime(adb, serial, before_value))
            update_transaction_state(state_path, state, "runtime-restored")
        run([str(adb), "-s", serial, "emu", "kill"], check=False, timeout=30)
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            if process_matches(state):
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            else:
                restore_errors.append("process-identity-mismatch")
        emulator_log.flush()
        os.fsync(emulator_log.fileno())
        emulator_log.close()
        if process.poll() is None:
            restore_errors.append("process-still-running")
        devices = ""
        disappearance_deadline = time.time() + 5
        while time.time() < disappearance_deadline:
            devices = run([str(adb), "devices"], check=False, timeout=30).stdout
            if serial not in devices:
                break
            time.sleep(0.2)
        if serial in devices:
            restore_errors.append("serial-still-present")
        try:
            restored_identity = avd_static_identity(repo, args.avd, args.snapshot)
        except (FileNotFoundError, KeyError) as error:
            restored_identity = None
            restore_errors.append(f"static-identity-missing:{type(error).__name__}")
        if restored_identity is not None and restored_identity != EXPECTED_AVD_IDENTITY:
            restore_errors.append("static-identity-drift")
        for signum, previous in previous_handlers.items():
            signal.signal(signum, previous)

    if failure is not None or restore_errors:
        state["status"] = "failed"
        state["failure"] = f"{type(failure).__name__}:{failure}" if failure else ""
        state["restore_errors"] = restore_errors
        update_transaction_state(state_path, state, "failed")
        if restore_errors:
            for error in restore_errors:
                print(f"D0_AVD_RESTORE=FAIL FIELD={error}")
        print(
            "D0_CAPTURE=FAIL IDENTITY_MATCH=1 RESTORE_MATCH="
            + ("0" if restore_errors else "1")
            + f" FIELD=runtime ACTUAL={type(failure).__name__ if failure else 'restore'}",
        )
        raise SystemExit(2)

    state["status"] = "complete"
    state["restore_errors"] = []
    update_transaction_state(state_path, state, "complete")
    assert capture_counts == (68, 68, 132, 12)
    print(
        "D0_CAPTURE=PASS SCREENSHOTS=68 SEMANTICS_FILES=68 SEMANTICS_RECORDS=132 "
        "DEFERRED_D5=20 SKIPPED=0 IDENTITY_MATCH=1 RESTORE_MATCH=1",
    )


def recover(args: argparse.Namespace) -> None:
    state_path = Path(args.state).resolve()
    state = json.loads(state_path.read_text(encoding="utf-8"))
    repo = Path(str(state["repo"])).resolve()
    adb = Path(str(state["adb"])).resolve()
    serial = str(state["serial"])
    pid = int(state["pid"])
    errors: list[str] = []
    command = process_field(pid, "command")
    if command and not process_matches(state):
        print("D0_AVD_RECOVERY=FAIL FIELD=process-identity")
        raise SystemExit(2)
    devices = run([str(adb), "devices"], check=False, timeout=30).stdout
    if serial in devices:
        try:
            avd_output = adb_text(adb, serial, "emu", "avd", "name")
            if avd_output.splitlines()[0] != state["avd"]:
                raise RuntimeError("AVD name mismatch")
            before = state.get("before")
            if isinstance(before, dict):
                errors.extend(restore_avd_runtime(adb, serial, before))
            run([str(adb), "-s", serial, "emu", "kill"], check=False, timeout=30)
        except BaseException as error:  # noqa: BLE001 - owned PID teardown still proceeds.
            errors.append(f"adb-recovery:{type(error).__name__}:{error}")
    deadline = time.time() + 10
    while command and process_field(pid, "command") and time.time() < deadline:
        time.sleep(0.2)
    if process_field(pid, "command"):
        if not process_matches(state):
            errors.append("process-identity-changed")
        else:
            os.kill(pid, signal.SIGTERM)
            deadline = time.time() + 10
            while process_field(pid, "command") and time.time() < deadline:
                time.sleep(0.2)
            if process_field(pid, "command"):
                os.kill(pid, signal.SIGKILL)
    if process_field(pid, "command"):
        errors.append("process-still-running")
    devices = run([str(adb), "devices"], check=False, timeout=30).stdout
    if serial in devices:
        errors.append("serial-still-present")
    try:
        identity = avd_static_identity(repo, str(state["avd"]), str(state["snapshot"]))
    except (FileNotFoundError, KeyError) as error:
        identity = None
        errors.append(f"static-identity-missing:{type(error).__name__}")
    if identity is not None and identity != EXPECTED_AVD_IDENTITY:
        errors.append("static-identity-drift")
    if errors:
        state["status"] = "recovery-failed"
        state["restore_errors"] = errors
        update_transaction_state(state_path, state, "recovery-failed")
        print("D0_AVD_RECOVERY=FAIL " + " ".join(errors))
        raise SystemExit(2)
    state["status"] = "recovered"
    state["restore_errors"] = []
    update_transaction_state(state_path, state, "recovered")
    print("D0_AVD_RECOVERY=PASS RESTORE_MATCH=1 PROCESS_GONE=1")


def pack_goldens(input_dir: Path, matrix: Path, output: Path) -> str:
    counts = validate_capture_files(input_dir, matrix)
    if counts != (68, 68, 132, 12):
        raise ValueError(f"capture count mismatch before pack: {counts}")
    rows = read_tsv(matrix)
    active = [row for row in rows if row["action"] == "capture"]
    expected = sorted(path for row in active for path in (row["screenshot"], row["semantics"]))
    missing = [name for name in expected if not (input_dir / name).is_file()]
    if missing:
        raise ValueError("missing capture files: " + ",".join(missing[:10]))
    for row in active:
        row["screenshot_sha256"] = sha256_file(input_dir / row["screenshot"])
        row["semantics_sha256"] = sha256_file(input_dir / row["semantics"])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
        temporary_name = Path(temporary.name)
    try:
        with tarfile.open(temporary_name, "w") as archive:
            for name in expected:
                payload = (input_dir / name).read_bytes()
                info = tarfile.TarInfo(name)
                info.size = len(payload)
                info.mtime = 0
                info.uid = info.gid = 0
                info.uname = info.gname = ""
                info.mode = 0o644
                archive.addfile(info, io.BytesIO(payload))
        os.replace(temporary_name, output)
    finally:
        temporary_name.unlink(missing_ok=True)
    atomic_write(matrix, tsv_bytes(SURFACE_FIELDS, rows))
    return sha256_file(output)


D0_ALLOWED_PATHS = {
    "AGENTS.md",
    "app/build.gradle.kts",
    "docs/analysis/architecture-review-log.md",
    "docs/analysis/kotlin-clean-architecture-d0.md",
    "docs/plan/43499-kotlin-package-refactor-plan.md",
    "docs/plan/kotlin-clean-architecture-ui-plan.md",
    "docs/plan/kotlin-clean-architecture-ui-plan-annex-a.md",
    "docs/plan/kotlin-clean-architecture-ui-plan-annex-b.md",
    "docs/plan/kotlin-clean-architecture-ui-plan-v0-checklist.md",
    "tools/kotlin_architecture_d0.py",
}

D0_ALLOWED_PREFIXES = (
    "app/src/test/kotlin/com/ghostlock/app/architecture/",
    "app/src/androidTest/kotlin/com/ghostlock/app/architecture/",
    "app/src/test/resources/architecture/",
)


def is_d0_allowed_path(path: str) -> bool:
    return path in D0_ALLOWED_PATHS or path.startswith(D0_ALLOWED_PREFIXES)


def outside_scope_status(repo: Path) -> list[str]:
    completed = run(
        ["git", "status", "--short", "--untracked-files=all"],
        check=True,
    )
    result: list[str] = []
    for line in completed.stdout.splitlines():
        path = line[3:]
        candidates = [item.strip() for item in path.split(" -> ")]
        if not candidates or not all(is_d0_allowed_path(item.strip('"')) for item in candidates):
            result.append(line)
    return result


def source_baseline_path(repo: Path) -> Path:
    return repo / "build/reports/kotlin-architecture-d0/source-baseline.json"


def record_source_baseline(repo: Path) -> None:
    path = source_baseline_path(repo)
    if path.exists():
        return
    payload = {
        "branch": run(["git", "branch", "--show-current"]).stdout.strip(),
        "head": run(["git", "rev-parse", "HEAD"]).stdout.strip(),
        "outside_scope_status": outside_scope_status(repo),
    }
    atomic_write(path, json.dumps(payload, ensure_ascii=False, indent=2, sort_keys=True).encode() + b"\n")


def command_inventory(args: argparse.Namespace) -> None:
    repo = Path(args.repo).resolve()
    output = Path(args.out).resolve()
    record_source_baseline(repo)
    write_inventory(repo, output)
    result = verify_inventory(repo, output)
    print(
        "D0_INVENTORY=PASS "
        + " ".join(f"{key}={value}" for key, value in result.items())
        + " API_KEY_COLLISIONS=0 UNCLASSIFIED=0",
    )


def command_verify(args: argparse.Namespace) -> None:
    result = verify_inventory(Path(args.repo).resolve(), Path(args.fixtures).resolve(), args.section)
    print("D0_VERIFY=PASS " + " ".join(f"{key}={value}" for key, value in result.items()))


def command_pack(args: argparse.Namespace) -> None:
    digest = pack_goldens(Path(args.input).resolve(), Path(args.matrix).resolve(), Path(args.output).resolve())
    print(f"D0_PACK=PASS SCREENSHOTS=68 DEFERRED_D5=20 MISSING=0 EXTRA=0 NORMALIZED_MTIME=0 SHA256={digest}")


def command_final(args: argparse.Namespace) -> None:
    repo = Path(args.repo).resolve()
    fixtures = Path(args.fixtures).resolve()
    verify_inventory(repo, fixtures)
    golden = Path(args.goldens).resolve()
    rows = read_tsv(fixtures / "ui-surface-matrix-v1.tsv")
    active = [row for row in rows if row["action"] == "capture"]
    expected = sorted(path for row in active for path in (row["screenshot"], row["semantics"]))
    with tarfile.open(golden, "r") as archive:
        members = [member for member in archive.getmembers() if member.isfile()]
        names = sorted(member.name for member in members)
        member_payloads = {
            member.name: archive.extractfile(member).read()  # type: ignore[union-attr]
            for member in members
        }
    if names != expected:
        raise ValueError(f"golden members mismatch expected={len(expected)} actual={len(names)}")
    for row in active:
        screenshot_hash = row.get("screenshot_sha256", "")
        semantics_hash = row.get("semantics_sha256", "")
        if re.fullmatch(r"[0-9a-f]{64}", screenshot_hash) is None:
            raise ValueError(f"missing screenshot hash: {row['member_id']}")
        if re.fullmatch(r"[0-9a-f]{64}", semantics_hash) is None:
            raise ValueError(f"missing semantics hash: {row['member_id']}")
        if sha256_bytes(member_payloads[row["screenshot"]]) != screenshot_hash:
            raise ValueError(f"golden screenshot hash drift: {row['member_id']}")
        if sha256_bytes(member_payloads[row["semantics"]]) != semantics_hash:
            raise ValueError(f"golden semantics hash drift: {row['member_id']}")
    baseline_path = source_baseline_path(repo)
    if not baseline_path.is_file():
        raise ValueError("D0 source baseline missing; rerun inventory")
    baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    if outside_scope_status(repo) != baseline["outside_scope_status"]:
        raise ValueError("out-of-scope git status changed since D0 inventory")
    with tempfile.TemporaryDirectory(prefix="ghostlock-d0-final-") as temporary_directory:
        regenerated = Path(temporary_directory)
        shutil.copy2(fixtures / "ui-surface-matrix-v1.tsv", regenerated / "ui-surface-matrix-v1.tsv")
        write_inventory(repo, regenerated)
        for name in TSV_NAMES:
            if sha256_file(regenerated / name) != sha256_file(fixtures / name):
                raise ValueError(f"manifest drift: {name}")
    print("D0_FINAL=PASS MANIFEST_SHA_MATCH=5 GOLDEN_SHA_MATCH=1 OUT_OF_SCOPE=0")


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser()
    sub = root.add_subparsers(dest="command", required=True)
    inventory = sub.add_parser("inventory")
    inventory.add_argument("--repo", required=True)
    inventory.add_argument("--out", required=True)
    inventory.set_defaults(handler=command_inventory)
    verify = sub.add_parser("verify")
    verify.add_argument("--repo", required=True)
    verify.add_argument("--fixtures", required=True)
    verify.add_argument(
        "--section",
        choices=("all", "architecture", "inventory", "component", "ksu", "surface"),
        default="all",
    )
    verify.set_defaults(handler=command_verify)
    capture_parser = sub.add_parser("capture")
    capture_parser.add_argument("--repo", default=".")
    capture_parser.add_argument("--avd", required=True)
    capture_parser.add_argument("--snapshot", required=True)
    capture_parser.add_argument("--port", type=int, required=True)
    capture_parser.add_argument("--debug-apk", required=True)
    capture_parser.add_argument("--test-apk", required=True)
    capture_parser.add_argument("--matrix", required=True)
    capture_parser.add_argument("--class", dest="test_class", required=True)
    capture_parser.add_argument("--out", required=True)
    capture_parser.set_defaults(handler=capture)
    recover_parser = sub.add_parser("recover")
    recover_parser.add_argument("--state", required=True)
    recover_parser.set_defaults(handler=recover)
    pack_parser = sub.add_parser("pack")
    pack_parser.add_argument("--input", required=True)
    pack_parser.add_argument("--matrix", required=True)
    pack_parser.add_argument("--output", required=True)
    pack_parser.set_defaults(handler=command_pack)
    final_parser = sub.add_parser("final")
    final_parser.add_argument("--repo", required=True)
    final_parser.add_argument("--fixtures", required=True)
    final_parser.add_argument("--goldens", required=True)
    final_parser.set_defaults(handler=command_final)
    return root


def main() -> None:
    args = parser().parse_args()
    args.handler(args)


if __name__ == "__main__":
    main()
