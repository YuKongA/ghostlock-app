#!/usr/bin/env python3
"""A1/A2 finish victim-only 静态 gate（task-68/task-72）。

判定（生产源码，注释先剥离）：
1) `backend/cve43499/**` 与 `ui/.../backends/**` 内**禁止** App/su 执行路径：
   - `ProcessBuilder(` 构造调用（ksud 证据命令 seam 与 legacy victim 路径在豁免清单）；
   - `Runtime.exec(` / `Runtime.getRuntime().exec(`；
   - `"su -c"` / `"su", "-c"` 提权命令构造；
   - `FinishExecution(` 构造调用（Deprecated 类定义文件本身豁免）。
2) 要求：
   - `Flow43499Impl.kt` 存在 `finishExecuteStep`、`victimFinish(`、token
     `cve43499:finish.execute:su-fallback-refused`，且**不得**存在 App executor 调用
     （`finishExecutor?.invoke` / `finishExecutor!!.invoke`）；
   - `Flow43499RunSpec.kt` 的 `finishExecutor` 字段附近有 `@Deprecated`；
   - `ui/.../FinishExecution.kt` 标注 `@Deprecated`；
   - `FinishHandoffAdapter.kt` 不再构造/传递 executor（无 `FinishExecution(`、无
     `finishExecutor =`）。
3) `--self-test` 负例自检：临时构造违规字符串/临时文件 ⇒ 扫描必须命中（检测失败则
   EXIT=1）；干净样本必须不命中。fixture 跑完删除。

用法（host；静态，不编译）：
    python3 tools/verify_43499_finish_victim_only.py
    python3 tools/verify_43499_finish_victim_only.py --self-test
退出码：0=PASS；1=违规/自检失败；2=环境缺失。
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BACKEND_DIR = REPO_ROOT / "app/src/main/kotlin/com/ghostlock/app/backend/cve43499"
UI_BACKENDS_DIR = REPO_ROOT / "app/src/main/kotlin/com/ghostlock/app/ui/screen/module/backends"

# 豁免文件（相对 repo 的路径后缀 → 理由）：
# - FinishExecution.kt 本身是 Deprecated 类的实现（本批保留兼容/待删；无生产调用）；
# - FinishExecutionEvidence.kt 的 ProcessBuilderFinishCommandSource 是 ksud 证据命令
#   seam（非 finish 脚本执行）——A1 只禁 finish 执行路径；
# - ProcessVictim.kt 是旧 Kotlin victim 子进程路径（另批迁移；A1 不动 victim 默认路径）。
EXEMPT_SUFFIXES: dict[str, str] = {
    "ui/screen/module/backends/FinishExecution.kt": "deprecated finish execution class definition",
    "finish/FinishExecutionEvidence.kt": "ksud evidence command source (not finish execution)",
    "victim/ProcessVictim.kt": "legacy Kotlin victim process path (separate batch)",
    "victim/ProcessVictimSession.kt": "legacy Kotlin victim session path (separate batch)",
}

FORBIDDEN_PATTERNS: list[tuple[str, re.Pattern[str]]] = [
    ("process-builder", re.compile(r"\bProcessBuilder\s*\(")),
    ("runtime-exec", re.compile(r"\bRuntime\s*\.\s*(?:getRuntime\s*\(\s*\)\s*\.\s*)?exec\s*\(")),
    ("su-command", re.compile(r'"su -c"|"su"\s*,\s*"-c"')),
    ("finish-execution-call", re.compile(r"\bFinishExecution\s*\(")),
]

REQUIREMENTS: list[tuple[str, Path, list[str], list[str]]] = [
    # (label, file, required substrings, forbidden substrings)
    (
        "flow-victim-only",
        BACKEND_DIR / "flow/Flow43499Impl.kt",
        [
            "finishExecuteStep",
            "victimFinish(",
            "cve43499:finish.execute:su-fallback-refused",
        ],
        ["finishExecutor?.invoke", "finishExecutor!!.invoke"],
    ),
    ("runspec-deprecated", BACKEND_DIR / "flow/Flow43499RunSpec.kt", ["@Deprecated", "finishExecutor"], []),
    (
        "finish-execution-deprecated",
        UI_BACKENDS_DIR / "FinishExecution.kt",
        ["@Deprecated"],
        [],
    ),
    (
        "adapter-no-executor",
        UI_BACKENDS_DIR / "FinishHandoffAdapter.kt",
        [],
        ["FinishExecution(", "finishExecutor ="],
    ),
    (
        "finish-timeout-detach",
        BACKEND_DIR / "victim/ProcessVictimSession.kt",
        ["victim-finish:timeout", "retained = true", "park"],
        [],
    ),
    (
        "close-contract-seven",
        BACKEND_DIR / "flow/Flow43499Impl.kt",
        [
            "requireCleanCloseAfterCred",
            "w2-chain",
            "prebuilt-repair",
            "w2-fallback",
            "prebuild-discard",
            "activation",
            "clean-failure",
            "closeAll",
        ],
        [],
    ),
]


def strip_comments(text: str) -> str:
    """剥离 Kotlin 行注释与块注释（字符串字面量内的 // 不在本例出现，保持简单）。"""
    out: list[str] = []
    i = 0
    in_line = False
    in_block = False
    while i < len(text):
        if in_line:
            if text[i] == "\n":
                in_line = False
                out.append("\n")
            i += 1
            continue
        if in_block:
            if text.startswith("*/", i):
                in_block = False
                i += 2
                out.append(" ")
            else:
                i += 1
            continue
        if text.startswith("//", i):
            in_line = True
            i += 2
            continue
        if text.startswith("/*", i):
            in_block = True
            i += 2
            out.append(" ")
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def scan_text(text: str) -> list[str]:
    """扫描一个 Kotlin 源码文本，返回违规描述（剥离注释后）。"""
    stripped = strip_comments(text)
    violations: list[str] = []
    for name, pattern in FORBIDDEN_PATTERNS:
        if pattern.search(stripped):
            violations.append(name)
    return violations


def iter_production_files() -> list[Path]:
    files: list[Path] = []
    for root in (BACKEND_DIR, UI_BACKENDS_DIR):
        if not root.is_dir():
            continue
        files.extend(sorted(root.rglob("*.kt")))
    return files


def relative(path: Path) -> str:
    return path.relative_to(REPO_ROOT).as_posix()


def exempt_reason(path: Path) -> str | None:
    rel = relative(path)
    for suffix, reason in EXEMPT_SUFFIXES.items():
        if rel.endswith(suffix):
            return reason
    return None


def scan_finish_block(text: str) -> list[str]:
    """检查 ProcessVictimSession.finish 超时分支不得 close/kill victim（A1 delta/I2）。

    @param text Kotlin 源码（未剥注释也可，这里只找 finish 函数块的朴素花括号范围）。
    @return 违规 token 列表。
    """
    marker = "override fun finish("
    start = text.find(marker)
    if start < 0:
        return ["missing-finish-function"]
    brace_start = text.find("{", start)
    if brace_start < 0:
        return ["missing-finish-body"]
    depth = 0
    end = -1
    for index in range(brace_start, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                end = index + 1
                break
    if end < 0:
        return ["unterminated-finish-body"]
    block = text[brace_start:end]
    forbidden = ["closeLocked(", "killVictim(", "kill(", "destroy(", "destroyForcibly("]
    return [token for token in forbidden if token in block]


def run_gate() -> int:
    files = iter_production_files()
    if not files:
        print("finish-victim-only gate: FAIL no production sources found", flush=True)
        return 2
    violations: list[str] = []
    exempted: list[str] = []
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        found = scan_text(text)
        if path.name == "ProcessVictimSession.kt":
            found += scan_finish_block(text)
        if found:
            reason = exempt_reason(path)
            if reason is not None:
                exempted.append(f"{relative(path)}: {','.join(found)} ({reason})")
                continue
            violations.append(f"{relative(path)}: {','.join(found)}")
    for label, path, required, forbidden in REQUIREMENTS:
        if not path.is_file():
            violations.append(f"{label}: missing file {relative(path)}")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for token in required:
            if token not in text:
                violations.append(f"{label}: missing required token {token!r} in {relative(path)}")
        for token in forbidden:
            if token in text:
                violations.append(f"{label}: forbidden token {token!r} in {relative(path)}")
    print(
        f"scan[finish-victim-only]: files={len(files)} "
        f"exempt={len(exempted)} violations={len(violations)}",
        flush=True,
    )
    for line in exempted:
        print(f"  exempt: {line}", flush=True)
    for line in violations:
        print(f"  violation: {line}", flush=True)
    if violations:
        print("finish-victim-only gate: FAIL", flush=True)
        return 1
    print("finish-victim-only gate: PASS (no App/su finish execution; rooted victim only)", flush=True)
    return 0


def run_self_test() -> int:
    """负例自检：违规样本必须命中，干净样本必须不命中，临时文件检测后删除。"""
    failures = 0
    bad_samples = [
        "val p = ProcessBuilder(listOf(\"su\")).start()",
        "Runtime.exec(arrayOf(\"su\", \"-c\", \"id\"))",
        "Runtime.getRuntime().exec(\"su -c id\")",
        "val cmd = listOf(\"su\", \"-c\", \"/system/bin/sh x\")",
        "FinishExecution().execute(handoff, runId)",
    ]
    for sample in bad_samples:
        if not scan_text(sample):
            print(f"  self-test FAIL undetected: {sample}", flush=True)
            failures += 1
    # A2 负例：finish 路径 ProcessBuilder / timeout kill victim / 生产 FinishExecution 引用。
    finish_block_bad = "override fun finish() {\n val p = ProcessBuilder(listOf(\"su\")).start()\n}"
    if not scan_text(finish_block_bad):
        print("  self-test FAIL finish-path ProcessBuilder undetected", flush=True)
        failures += 1
    timeout_kill_bad = (
        "override fun finish(scriptPath: String, timeoutMs: Long): VictimFinishResult {\n"
        "  try { run() } catch (timeout: TimeoutException) { closeLocked() }\n"
        "  return ok\n}"
    )
    if not scan_finish_block(timeout_kill_bad):
        print("  self-test FAIL timeout-kill victim undetected", flush=True)
        failures += 1
    prod_reference = "val fn = { script: Any -> FinishExecution().execute(script) }"
    if "finish-execution-call" not in scan_text(prod_reference):
        print("  self-test FAIL production FinishExecution reference undetected", flush=True)
        failures += 1
    clean_samples = [
        "victim.finish(scriptPath, timeoutMs)",
        "val evidence = FinishExecutionEvidence(runId, 0, false, \"sha\", null, null)",
        "ProcessBuilderFinishCommandSource",
        "catch (timeout: TimeoutException) { detachForFinishTimeoutLocked() }",
    ]
    for sample in clean_samples:
        if scan_text(sample):
            print(f"  self-test FAIL false-positive: {sample}", flush=True)
            failures += 1
    with tempfile.TemporaryDirectory(prefix="glk43499-finish-gate-") as tmp:
        fixture = Path(tmp) / "BadFinish.kt"
        fixture.write_text("internal fun bad() = FinishExecution().execute(1, 2)\n", encoding="utf-8")
        found = scan_text(fixture.read_text(encoding="utf-8"))
        if "finish-execution-call" not in found:
            print("  self-test FAIL temp fixture not detected", flush=True)
            failures += 1
        fixture.unlink(missing_ok=True)
    if failures:
        print(f"finish-victim-only self-test: FAIL failures={failures}", flush=True)
        return 1
    print("finish-victim-only self-test: PASS (violations detected; clean samples pass)", flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="A1 finish victim-only 静态 gate")
    parser.add_argument("--self-test", action="store_true", help="只跑负例/干净样本自检")
    args = parser.parse_args()
    if args.self_test:
        return run_self_test()
    return run_gate()


if __name__ == "__main__":
    sys.exit(main())
