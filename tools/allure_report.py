"""Allure owns statistics/reports; this bridge only adapts CTest and applies gate policy."""

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


REPO = Path(__file__).resolve().parents[1]
VERSION = "3.33.0"


def allure_command(install=False):
    home = REPO / "build/tooling/allure"
    package = home / "node_modules/allure/package.json"
    executable = home / "node_modules/.bin/allure"
    try:
        ready = (
            package.is_file()
            and json.loads(package.read_text()).get("version") == VERSION
            and executable.is_file()
            and os.access(executable, os.X_OK)
        )
    except (OSError, json.JSONDecodeError):
        ready = False
    if not ready:
        if not install:
            raise ValueError("Allure dependency is missing; run the existing Gradle result gate to provision it")
        npm = shutil.which("npm") or "/opt/homebrew/bin/npm"
        subprocess.run([npm, "install", "--prefix", str(home), "--ignore-scripts",
                        "--no-audit", "--no-fund", "--save-exact", f"allure@{VERSION}"], check=True)
        if not (
            package.is_file()
            and json.loads(package.read_text()).get("version") == VERSION
            and executable.is_file()
            and os.access(executable, os.X_OK)
        ):
            raise ValueError("Allure install completed without the pinned package metadata and executable")
    return str(executable)


def prepare_inputs(directories, ctest, staging):
    inputs = []
    for index, directory in enumerate(directories):
        files = sorted(directory.rglob("*.xml"))
        has_cases = False
        target = staging / str(index)
        if ctest:
            target.mkdir()
        for xml in files:
            tree = ET.parse(xml)  # A corrupt producer must not silently disappear from Allure.
            cases = list(tree.iter("testcase"))
            has_cases |= bool(cases)
            if ctest:
                for case in cases:
                    # CTest's disabled attribute is not a JUnit skip; keep the raw XML unchanged.
                    if case.get("status") == "disabled" and case.find("skipped") is None:
                        ET.SubElement(case, "skipped", message="Disabled by CTest")
                    # Equal test names from different CTest modules are not retries.
                    case.set("classname", directory.parent.name + "." + case.get("classname", "CTest"))
                tree.write(target / xml.name, encoding="utf-8", xml_declaration=True)
        if not has_cases:
            raise ValueError(f"missing test results for required producer: {directory}")
        inputs.append(str(target if ctest else directory))
    return inputs


def main():
    build = REPO / "build"
    os.environ["ALLURE_AGENT_STATE_DIR"] = str(build / "allure-agent-state")
    os.environ["npm_config_cache"] = str(build / "tooling/npm-cache")
    # Gradle daemons on macOS may lack the Homebrew Node directory in PATH.
    os.environ["PATH"] += os.pathsep + "/opt/homebrew/bin"
    if sys.argv[1:] == ["--install"]:
        allure_command(install=True)
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scope", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ctest", action="store_true")
    parser.add_argument("--allow-skips", action="store_true")
    parser.add_argument("--runner-failed", action="store_true")
    parser.add_argument("directories", nargs="+", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(build) or output == build:
        raise ValueError("Allure output must be a dedicated directory under build/")
    shutil.rmtree(output, ignore_errors=True)
    cli = allure_command()
    with tempfile.TemporaryDirectory(prefix="allure-input-", dir=build) as temporary:
        inputs = prepare_inputs(args.directories, args.ctest, Path(temporary))
        subprocess.run([cli, "agent", "inspect", *inputs, "--output", str(output),
                        "--environment", args.scope, "--report", "auto"], check=True, cwd=REPO)
    result = subprocess.run([cli, "agent", "query", "--from", str(output), "summary"],
                            check=True, capture_output=True, text=True, cwd=REPO)
    (output / "summary.json").write_text(result.stdout)
    summary = json.loads(result.stdout)["summary"]
    stats = summary["stats"]
    # No suite-header arithmetic or historical expected count: consume Allure's actual statuses.
    failed = stats.get("failed", 0) + stats.get("broken", 0)
    skipped = stats.get("skipped", 0)
    valid = (not args.runner_failed and stats["total"] > 0 and not failed and not stats.get("unknown", 0)
             and (args.allow_skips or not skipped)
             and summary["compact"]["completeness"] == "complete")
    status = ("PASS_WITH_SKIPS" if skipped else "PASS") if valid else "FAIL"
    print(f"ALLURE_GATE={status} SCOPE={args.scope} TOTAL={stats['total']} "
          f"PASSED={stats.get('passed', 0)} FAILED={failed} SKIPPED={skipped} "
          f"UNKNOWN={stats.get('unknown', 0)} REPORT={output / 'index.md'}", flush=True)
    return 0 if valid else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, ET.ParseError, subprocess.CalledProcessError) as error:
        print(f"ALLURE_GATE=ERROR: {error}", file=sys.stderr)
        sys.exit(1)
