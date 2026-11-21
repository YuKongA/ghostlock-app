"""Exercise the root Gradle implementation, not a second XML/counting algorithm."""

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
MODULES = ["nativeTest43499GluedHost", "nativeTestContractsHost", "avbcheck"]
PASS = '<testcase name="pass" status="run"/>'
FAIL = '<testcase name="fail" status="fail"><failure/></testcase>'
ERROR = '<testcase name="error" status="fail"><error/></testcase>'
SKIP = '<testcase name="skip" status="notrun"><skipped/></testcase>'
DISABLED = '<testcase name="disabled" status="disabled"/>'


def suite(cases, tests, failures=0, skipped=0, disabled=0, errors=0):
    return (f'<testsuite tests="{tests}" failures="{failures}" skipped="{skipped}" '
            f'disabled="{disabled}" errors="{errors}">{cases}</testsuite>')


class NativeTestSummaryTests(unittest.TestCase):
    def setUp(self):
        (REPO / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="native-summary-test-", dir=REPO / "build")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.init = self.root / "test.init.gradle"

    def gradle(self, *tasks, cache=True):
        result = subprocess.run(
            [str(REPO / "gradlew"), "--console=plain", "--offline", "-I", str(self.init),
             "--configuration-cache" if cache else "--no-configuration-cache", *tasks],
            cwd=REPO, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
        )
        return result

    def configure_real_ctest_fixture(self, fail=False):
        source = self.root / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.21)
project(NativeSummaryRegression NONE)
enable_testing()
add_test(NAME pass COMMAND "${CMAKE_COMMAND}" -E true)
add_test(NAME skip COMMAND "${CMAKE_COMMAND}" -E false)
set_tests_properties(skip PROPERTIES SKIP_RETURN_CODE 1)
add_test(NAME disabled COMMAND "${CMAKE_COMMAND}" -E true)
set_tests_properties(disabled PROPERTIES DISABLED TRUE)
''' + ('add_test(NAME fail COMMAND "${CMAKE_COMMAND}" -E false)\n' if fail else ""))
        self.init.write_text(f'''
gradle.projectsEvaluated {{
    {json.dumps(MODULES)}.each {{ taskName ->
        gradle.rootProject.tasks.named(taskName).configure {{
            sourceDir.set(new File({json.dumps(str(source))}))
            outputDir.set(new File({json.dumps(str(self.root))}, taskName))
            buildTargets.set([])
        }}
    }}
}}
''')

    def test_actual_results_cache_reuse_and_excluded_producer(self):
        self.configure_real_ctest_fixture()
        expected = "ALLURE_GATE=PASS_WITH_SKIPS SCOPE=host-native TOTAL=9 PASSED=3 FAILED=0 SKIPPED=6"
        for invocation in range(2):
            result = self.gradle("nativeTestHost")
            self.assertEqual(0, result.returncode, result.stdout)
            self.assertIn(expected, result.stdout)
            self.assertIn("HOST_SOURCES=31", result.stdout)
            if invocation:
                self.assertIn("Configuration cache entry reused.", result.stdout)
        # The preceding build left valid XML, but excluded producers supplied no current results.
        result = self.gradle("nativeTestHost", "-x", "avbcheck")
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn("missing current-run host test results: [avbcheck]", result.stdout)
        self.assertNotIn("ALLURE_GATE=PASS SCOPE=host-native", result.stdout)

    def test_real_ctest_failure_is_counted_before_original_gate_fails(self):
        self.configure_real_ctest_fixture(fail=True)
        aggregate = REPO / "build/reports/allure/host-native"
        aggregate.mkdir(parents=True, exist_ok=True)
        (aggregate / "index.md").write_text("previous run only")
        result = self.gradle("nativeTestHost")
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn("ALLURE_GATE=FAIL", result.stdout)
        self.assertIn("TOTAL=4 PASSED=1 FAILED=1 SKIPPED=2", result.stdout)
        self.assertIn("CTEST_EXIT=8", result.stdout)
        self.assertNotIn("ALLURE_GATE=PASS SCOPE=host-native", result.stdout)
        self.assertFalse((aggregate / "index.md").exists())


    def test_allure_statuses_and_required_producer_inputs(self):
        inputs = self.root / "results"
        inputs.mkdir()
        output = self.root / "report"
        command = [sys.executable, str(REPO / "tools/allure_report.py"),
                   "--scope", "regression", "--output", str(output)]
        cases = (
            (suite(PASS, 155), False, 0, "TOTAL=1 PASSED=1 FAILED=0 SKIPPED=0"),
            (suite(FAIL + ERROR + PASS, 3), False, 1, "TOTAL=3 PASSED=1 FAILED=2 SKIPPED=0"),
            (suite(SKIP + DISABLED + PASS, 3), True, 0, "TOTAL=3 PASSED=1 FAILED=0 SKIPPED=2"),
            (suite(SKIP, 1), False, 1, "TOTAL=1 PASSED=0 FAILED=0 SKIPPED=1"),
        )
        for xml, ctest, code, expected in cases:
            with self.subTest(expected=expected):
                (inputs / "tests.xml").write_text(xml)
                args = ["--ctest", "--allow-skips"] if ctest else []
                result = subprocess.run(command + args + [str(inputs)], text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
                self.assertEqual(code, result.returncode, result.stdout)
                self.assertIn(expected, result.stdout)
                self.assertTrue((output / "index.md").is_file())
                self.assertTrue((output / "awesome/index.html").is_file())
                self.assertEqual(xml, (inputs / "tests.xml").read_text())
        (inputs / "tests.xml").write_text(suite(PASS, 1))
        missing = self.root / "missing-producer"
        result = subprocess.run(command + [str(inputs), str(missing)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn("missing test results for required producer", result.stdout)
        self.assertFalse((output / "summary.json").exists())
        result = subprocess.run(command + ["--runner-failed", str(inputs)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(1, result.returncode, result.stdout)
        self.assertIn("ALLURE_GATE=FAIL", result.stdout)
        self.assertNotIn("ALLURE_GATE=PASS", result.stdout)

        unknown = inputs / "unknown-result.json"
        unknown.write_text(json.dumps({"uuid": "unknown-regression", "name": "unknown",
                                       "fullName": "Fixture.unknown", "status": "unknown"}))
        result = subprocess.run(command + [str(inputs)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        self.assertEqual(1, result.returncode, result.stdout)
        self.assertIn("UNKNOWN=1", result.stdout)
        unknown.unlink()
        (inputs / "corrupt.xml").write_text("<testsuite>")
        result = subprocess.run(command + [str(inputs)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertNotIn("ALLURE_GATE=PASS", result.stdout)

    def test_build_pollution_cleanup_preserves_report_dependencies(self):
        dependency = self.root / "node_modules/fixture/no-conflict.js"
        dependency.parent.mkdir(parents=True)
        dependency.write_text("package-managed file")
        original = self.root / "resource.xml"
        duplicate = self.root / "resource 2.xml"
        original.write_text("original")
        duplicate.write_text("sync copy")
        self.init.write_text("")
        result = self.gradle("cleanBuildPollution")
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertTrue(dependency.is_file())
        self.assertTrue(original.is_file())
        self.assertFalse(duplicate.exists())

    def test_prepare_allure_repairs_missing_cli(self):
        self.init.write_text("")
        result = self.gradle("prepareAllure")
        self.assertEqual(0, result.returncode, result.stdout)
        package = REPO / "build/tooling/allure/node_modules/allure/package.json"
        executable = REPO / "build/tooling/allure/node_modules/.bin/allure"
        self.assertEqual("3.33.0", json.loads(package.read_text())["version"])
        self.assertTrue(executable.is_file() and os.access(executable, os.X_OK))

        executable.unlink()
        self.assertTrue(package.is_file())
        self.assertFalse(executable.exists())
        result = self.gradle("prepareAllure")
        self.assertEqual(0, result.returncode, result.stdout)
        self.assertNotIn(":prepareAllure UP-TO-DATE", result.stdout)
        self.assertEqual("3.33.0", json.loads(package.read_text())["version"])
        self.assertTrue(executable.is_file() and os.access(executable, os.X_OK))


if __name__ == "__main__":
    unittest.main()
