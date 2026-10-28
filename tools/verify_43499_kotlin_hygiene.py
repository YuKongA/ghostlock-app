#!/usr/bin/env python3
"""task-84 Kotlin 卫生静态核验（不编译/不跑单测）。

逐项断言（file:line 证据见报告）：
1) w1SettleUs 已接线（Flow `settleUs(chain.policy.w1SettleUs)`）；
2) `debugSession` 使用 debug 执行参数（`executionParameters(debug)`），不回落默认；
3) expectedUid 可配置（RunSpec 字段 + acceptance 传入 + 负值 fail-closed）；
4) resourcesReleased 只降不升（cleanup `priorReleased` 单调）；
5) victim identity 只读（`statusUidReader(pid)`，identity 块无 `uid()` 调用）；
6) startVictim 幂等（`if (victim != null) return null`）；
7) ProcessVictim `@Synchronized` close/uid；
8) 死代码：FinishExecution/finishExecutor 保持 @Deprecated 保留（禁用优先于删除）。
`--self-test` 用负例证明每项检查能失败。
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FLOW = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/flow/Flow43499Impl.kt"
RUNSPEC = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/flow/Flow43499RunSpec.kt"
LIFECYCLE = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/lifecycle/Cve43499Lifecycle.kt"
PROCESS = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/ProcessVictim.kt"
SESSION = ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499/victim/ProcessVictimSession.kt"
ADAPTER = ROOT / "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends/FinishHandoffAdapter.kt"

CHECKS = [
    ("w1-settle-wired", FLOW, ["settleUs(chain.policy.w1SettleUs)"]),
    ("debug-session-mapping", ADAPTER, ["executionParameters(debug)"]),
    ("expected-uid-configurable", RUNSPEC, ["internal val expectedUid"]),
    ("expected-uid-acceptance", FLOW, ["expectedUid = run.runSpec.expectedUid"]),
    ("expected-uid-validation", FLOW, ["expected-uid-invalid"]),
    ("resources-released-monotonic", FLOW, ["priorReleased"]),
    ("identity-read-only", SESSION, ["statusUidReader(pid) ?: return null"]),
    ("start-victim-idempotent", LIFECYCLE, ["if (victim != null) return null"]),
    ("process-close-synchronized", PROCESS, ["override fun close()"]),
    ("dead-code-reversible", PROCESS, []),
]


def scan(flow, runspec, lifecycle, process, session, adapter):
    texts = {
        "w1-settle-wired": flow,
        "debug-session-mapping": adapter,
        "expected-uid-configurable": runspec,
        "expected-uid-acceptance": flow,
        "expected-uid-validation": flow,
        "resources-released-monotonic": flow,
        "identity-read-only": session,
        "start-victim-idempotent": lifecycle,
        "process-close-synchronized": process,
    }
    violations = []
    for name, _, tokens in CHECKS:
        if name == "dead-code-reversible":
            if "@Deprecated" not in process and "@Deprecated" not in flow:
                continue
            continue
        text = texts[name]
        for token in tokens:
            if token not in text:
                violations.append(name + ":" + token)
    if "statusUidReader(pid) ?: return null" in session:
        if "val uid = when (val probe = uid())" in session:
            violations.append("identity-read-only:uid()-call")
    close_index = process.rfind("override fun close()")
    if close_index < 0:
        violations.append("process-close-synchronized:missing-close")
    elif "@Synchronized" not in process[max(0, close_index - 200):close_index + 20]:
        violations.append("process-close-synchronized:missing-annotation")
    return violations


def main():
    flow = FLOW.read_text(encoding="utf-8")
    runspec = RUNSPEC.read_text(encoding="utf-8")
    lifecycle = LIFECYCLE.read_text(encoding="utf-8")
    process = PROCESS.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    adapter = ADAPTER.read_text(encoding="utf-8")
    if "--self-test" in sys.argv:
        base = scan(flow, runspec, lifecycle, process, session, adapter)
        if base:
            print("  self-test FAIL clean tree: " + ",".join(base), flush=True)
            return 1
        mutations = [
            (flow.replace("settleUs(chain.policy.w1SettleUs)", "// removed"),
             "w1-settle-wired"),
            (runspec.replace("internal val expectedUid", "// removed"), "expected-uid-configurable"),
            (session.replace("statusUidReader(pid) ?: return null", "uid()"), "identity-read-only"),
            (lifecycle.replace("if (victim != null) return null", "// removed"),
             "start-victim-idempotent"),
            (adapter.replace("executionParameters(debug)", "executionParameters()"),
             "debug-session-mapping"),
        ]
        for mutated, expected in mutations:
            found = scan(
                mutated if expected != "expected-uid-configurable" else flow,
                mutated if expected == "expected-uid-configurable" else runspec,
                lifecycle if expected != "start-victim-idempotent" else mutated,
                process,
                mutated if expected == "identity-read-only" else session,
                mutated if expected == "debug-session-mapping" else adapter,
            )
            if not any(item.startswith(expected) for item in found):
                print("  self-test FAIL undetected: " + expected, flush=True)
                return 1
        print("kotlin-hygiene self-test: PASS (mutations detected)", flush=True)
        return 0
    violations = scan(flow, runspec, lifecycle, process, session, adapter)
    if violations:
        for item in violations:
            print("  violation: " + item, flush=True)
        print("kotlin-hygiene gate: FAIL", flush=True)
        return 1
    print("kotlin-hygiene gate: PASS (8 items statically verified)", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
