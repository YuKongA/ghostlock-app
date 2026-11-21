"""Regress the existing app XML gates through their actual Gradle implementation."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
PRODUCER = ":app:testDebugUnitTest"
GATE = ":app:verifyDebugUnitTestXml"
PASS = '<testcase name="pass" classname="Fixture"/>'
FAIL = '<testcase name="fail" classname="Fixture"><failure/></testcase>'
ERROR = '<testcase name="error" classname="Fixture"><error/></testcase>'
SKIP = '<testcase name="skip" classname="Fixture"><skipped/></testcase>'


def suite(cases, tests, failures=0, errors=0, skipped=0, name="Fixture"):
    return (f'<testsuite name="{name}" tests="{tests}" failures="{failures}" '
            f'errors="{errors}" skipped="{skipped}">{cases}</testsuite>')


class UnitTestXmlTests(unittest.TestCase):
    def setUp(self):
        (REPO / "build").mkdir(exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="unit-xml-test-", dir=REPO / "build")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.init = self.root / "test.init.gradle"

    def gradle(self, *tasks, cache=True):
        return subprocess.run(
            [str(REPO / "gradlew"), "--console=plain", "--offline", "-I", str(self.init),
             "--configuration-cache" if cache else "--no-configuration-cache", *tasks],
            cwd=REPO, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
        )

    def real_test_fixture(self, fail=False):
        (self.root / "Fixture.java").write_text('''
public class Fixture {
    @org.junit.Test public void runs() throws Exception {
        java.nio.file.Files.writeString(java.nio.file.Path.of(System.getProperty("xml.marker")),
            "run\\n", java.nio.file.StandardOpenOption.CREATE, java.nio.file.StandardOpenOption.APPEND);
        FAIL
    }
}
'''.replace("FAIL", 'org.junit.Assert.fail("fixture failure");' if fail else ""))
        self.init.write_text('''
gradle.beforeProject { app ->
    if (app.path != ':app') return
    app.afterEvaluate {
    def root = new File(ROOT)
    def classes = new File(root, 'classes')
    classes.mkdirs()
    def empty = new File(root, 'empty')
    empty.mkdirs()
    def junit = app.configurations.detachedConfiguration(app.dependencies.create('junit:junit:4.13.2'))
    def jars = junit.files
    assert javax.tools.ToolProvider.systemJavaCompiler.run(null, null, null,
        '--release', '17', '-classpath', jars*.path.join(File.pathSeparator),
        '-d', classes.path, new File(root, 'Fixture.java').path) == 0
    boolean skip = app.hasProperty('xmlFixtureSkip')
    boolean noSource = app.hasProperty('xmlFixtureNoSource')
    gradle.projectsEvaluated {
    app.tasks.named('testDebugUnitTest').configure {
        setDependsOn([])
        testClassesDirs = app.files(noSource ? empty : classes)
        classpath = app.files(classes, jars)
        reports.junitXml.outputLocation.set(new File(root, 'xml'))
        reports.html.outputLocation.set(new File(root, 'html'))
        binaryResultsDirectory.set(new File(root, 'binary'))
        systemProperty('xml.marker', new File(root, 'executions').path)
        onlyIf { !skip }
    }
    }
    }
}
'''.replace("ROOT", json.dumps(str(self.root))))

    def test_fresh_gate_cached_configuration_and_skipped_producer(self):
        self.real_test_fixture()
        for invocation in range(2):
            result = self.gradle(PRODUCER)
            self.assertEqual(0, result.returncode, result.stdout)
            if invocation:
                self.assertRegex(result.stdout, PRODUCER + " (UP-TO-DATE|FROM-CACHE)")
        marker = self.root / "executions"
        self.assertEqual(["run"], marker.read_text().splitlines())

        for invocation in range(2):
            stale = self.root / "xml" / "old" / "stale.xml"
            stale.parent.mkdir(exist_ok=True)
            stale.write_text(suite(PASS, 1, name="Stale"))
            result = self.gradle(GATE)
            self.assertEqual(0, result.returncode, result.stdout)
            self.assertIn("TOTAL=1 PASSED=1 FAILED=0 SKIPPED=0", result.stdout)
            self.assertFalse(stale.exists())
            self.assertEqual(invocation + 2, len(marker.read_text().splitlines()))
            if invocation:
                self.assertIn("Configuration cache entry reused.", result.stdout)
        for args in (("-x", PRODUCER), ("-PxmlFixtureSkip",), ("-PxmlFixtureNoSource",)):
            with self.subTest(args=args):
                result = self.gradle(GATE, *args)
                self.assertNotEqual(0, result.returncode, result.stdout)
                self.assertNotIn("ALLURE_GATE=PASS", result.stdout)
                self.assertRegex(result.stdout,
                                 "no current-run unit-test execution evidence|missing current-run Allure verification")
                self.assertFalse((REPO / "build/reports/allure/android-debug/index.md").exists())
                self.assertEqual(3, len(marker.read_text().splitlines()))

    def test_test_failure_still_stops_the_existing_gate(self):
        self.real_test_fixture(fail=True)
        result = self.gradle(GATE)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn(PRODUCER + " FAILED", result.stdout)
        self.assertIn("ALLURE_GATE=FAIL", result.stdout)
        self.assertIn("TOTAL=1 PASSED=0 FAILED=1 SKIPPED=0", result.stdout)
        self.assertNotIn("ALLURE_GATE=PASS", result.stdout)


    def test_filtered_run_is_not_full_variant_evidence(self):
        self.real_test_fixture()
        result = self.gradle(PRODUCER, "--tests", "Fixture.runs", GATE)
        self.assertNotEqual(0, result.returncode, result.stdout)
        self.assertIn("full-variant verification requires an unfiltered Test", result.stdout)


if __name__ == "__main__":
    unittest.main()
