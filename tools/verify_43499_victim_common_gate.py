#!/usr/bin/env python3
"""task-86 A1/A2a 静态契约：common victim opt-in 接线 + 43499 不使用 VictimScriptLauncher。

断言：
1) DebugVictimTransport 含 COMMON，parse 接受 "common"；
2) Cve43499VictimLaunchSpecFactory 用符号 capabilities（10 项）且 argv/env 空；
3) Cve43499CommonVictimRuntime 用 core VictimSession/VictimNativeJni；
4) FinishHandoffAdapter 有 COMMON 分支并作为 victimFactory；
5) 43499 backend/UI 源码不引用 VictimScriptLauncher（Q5 事实）；
6) process/jni-spawn 路径未被删/禁用。
`--self-test` 变异负例证明检查可失败。
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONFIG = ROOT / "app/src/main/kotlin/com/ghostlock/app/data/cve43499/A301SoDebugConfig.kt"
FACTORY = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/Cve43499VictimLaunchSpecFactory.kt"
RUNTIME = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/Cve43499CommonVictimRuntime.kt"
ADAPTER = ROOT / "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishHandoffAdapter.kt"
POLICY = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/Cve43499VictimKillPolicy.kt"
JNI_VICTIM = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/Cve43499JniVictim.kt"
BACKEND = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499"
UI_BACKENDS = ROOT / "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends"


def scan(config, factory, runtime, adapter, forbidden_refs, policy="", jni_victim=""):
    violations = []
    if "COMMON" not in config or '"common"' not in config:
        violations.append("enum/parse-common")
    if "COMMON_CAPABILITIES" not in factory or "VictimCapability.PROBE_STATUS" not in factory:
        violations.append("factory-capabilities")
    if "helperArgv = emptyList()" not in factory or "helperEnv = emptyList()" not in factory:
        violations.append("factory-empty-argv-env")
    if "VictimNativeJni" not in runtime or "newVictimSession" not in runtime:
        violations.append("runtime-native/session")
    if "DebugVictimTransport.COMMON" not in adapter or "commonVictimRuntime" not in adapter:
        violations.append("adapter-common-wiring")
    if "victimTransport = debug.injection.victim" not in adapter:
        violations.append("runspec-transport-passthrough")
    if "JNI_SPAWN" not in adapter or "newVictimSession" not in adapter:
        violations.append("legacy-jni-path-removed")
    if "killOnTimeout" not in policy or "shouldCloseKill" not in policy:
        violations.append("kill-policy-fields")
    if "shouldCloseKill(rootedFlag)" not in jni_victim:
        violations.append("kill-policy-close-wiring")
    if "killPolicy = killPolicy" not in runtime:
        violations.append("runtime-policy-passthrough")
    if "policyOf" not in factory:
        violations.append("factory-policy-exposure")
    for path, text in forbidden_refs:
        if "VictimScriptLauncher" in text:
            violations.append("script-launcher-reference:" + path)
    return violations


def read_all():
    return (
        CONFIG.read_text(encoding="utf-8"),
        FACTORY.read_text(encoding="utf-8"),
        RUNTIME.read_text(encoding="utf-8"),
        ADAPTER.read_text(encoding="utf-8"),
    )


def read_policy():
    return (
        POLICY.read_text(encoding="utf-8"),
        JNI_VICTIM.read_text(encoding="utf-8"),
    )


def forbidden_refs():
    refs = []
    for base in (BACKEND, UI_BACKENDS):
        for path in base.rglob("*.kt"):
            refs.append((str(path.relative_to(ROOT)), path.read_text(encoding="utf-8")))
    return refs


def main():
    config, factory, runtime, adapter = read_all()
    policy, jni_victim = read_policy()
    refs = forbidden_refs()
    if "--self-test" in sys.argv:
        if scan(config, factory, runtime, adapter, refs, policy, jni_victim):
            print("  self-test FAIL clean tree", flush=True)
            return 1
        mutations = [
            (config.replace("COMMON,", "// removed").replace('"common"', '"//"'), factory, runtime, adapter, "enum/parse-common"),
            (config, factory.replace("COMMON_CAPABILITIES", "REMOVED"), runtime, adapter, "factory-capabilities"),
            (config, factory, runtime, adapter.replace("victimTransport = debug.injection.victim", "// removed"), "runspec-transport-passthrough"),
            (config, factory.replace("policyOf", "REMOVED"), runtime, adapter, "factory-policy-exposure"),
        ]
        for mutated_config, mutated_factory, mutated_runtime, mutated_adapter, expected in mutations:
            found = scan(mutated_config, mutated_factory, mutated_runtime, mutated_adapter, refs, policy, jni_victim)
            if not any(item.startswith(expected) for item in found):
                print("  self-test FAIL undetected: " + expected, flush=True)
                return 1
        print("victim-common self-test: PASS (mutations detected)", flush=True)
        return 0
    violations = scan(config, factory, runtime, adapter, refs, policy, jni_victim)
    if violations:
        for item in violations:
            print("  violation: " + item, flush=True)
        return 1
    print("victim-common gate: PASS (common opt-in; killPolicy wired; no script carrier refs)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
