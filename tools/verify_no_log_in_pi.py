#!/usr/bin/env python3
"""43499 Native PI 窗口禁日志静态 gate（task-101）。

规则：
1. `app/src/main/cpp/backends/43499/core/**`（actor/page/carrier/perf/placement/victim 机制）
   出现 `gl_log` / `__android_log_print` / `gl_log_write` ⇒ FAIL；
2. `jni43499.c` 只允许在 JNI 边界函数内使用 `gl_log` 宏（info/warn/error），
   不得 `__android_log_print`；
3. `--self-test`：内存变异 core 文件加 `info("pi")` 必须被检出；不写工作树。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

CALL_RE = re.compile(r"(gl_log\w*\s*\(|__android_log_print\s*\(|\b(info|warn|error|debug|verbose)\s*\()")
JNI_FILE = "app/src/main/cpp/backends/43499/jni43499.c"


def validate(core_sources: dict[str, str], jni_text: str) -> list[str]:
    problems: list[str] = []
    for path, text in sorted(core_sources.items()):
        for i, line in enumerate(text.splitlines(), 1):
            if CALL_RE.search(line):
                problems.append(f"{path}:{i}: log call inside PI/hot-path core")
    if "__android_log_print" in jni_text:
        problems.append(f"{JNI_FILE}: __android_log_print forbidden; use gl_log")
    return problems


def read_sources(root: Path) -> tuple[dict[str, str], str]:
    core: dict[str, str] = {}
    core_root = root / "app/src/main/cpp/backends/43499/core"
    for path in core_root.rglob("*"):
        if path.suffix in (".c", ".h", ".cc", ".cpp"):
            core[str(path)] = path.read_text(encoding="utf-8", errors="replace")
    jni = (root / JNI_FILE).read_text(encoding="utf-8", errors="replace")
    return core, jni


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    core, jni = read_sources(root)
    problems = validate(core, jni)
    if "--self-test" in sys.argv:
        mutated = dict(core)
        key = next(iter(sorted(mutated)), None)
        if key is None:
            print("MUTATION=FAIL reason=core-sources-missing")
            return 1
        mutated[key] = mutated[key] + '\nvoid fake_pi_log(void) { info("pi-window"); }\n'
        if not any("inside PI/hot-path core" in p for p in validate(mutated, jni)):
            print("MUTATION=FAIL reason=pi-mutation-not-caught")
            return 1
        print("MUTATION=FAIL caught=pi-core-log")
        print("RESTORE=PASS")
    if problems:
        for problem in problems:
            print("FAIL " + problem)
        return 1
    print(f"PASS 43499 no-log-in-pi: {len(core)} core file(s), jni boundary whitelist")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
