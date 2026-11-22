import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[1] / "kotlin_architecture_d0.py"
SPEC = importlib.util.spec_from_file_location("kotlin_architecture_d0", SOURCE)
d0 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(d0)


class ArchitectureInputTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.repo = Path(temporary.name)
        self.fixtures = self.repo / "missing-fixtures"

    def test_architecture_cli_needs_no_manifests_and_accepts_legacy_fixture_argument(self):
        for fixture_args in ([], ["--fixtures", str(self.fixtures)]):
            with self.subTest(fixture_args=fixture_args):
                args = d0.parser().parse_args([
                    "verify", "--repo", str(self.repo), "--section", "architecture",
                    *fixture_args,
                ])
                output = io.StringIO()
                with patch.object(d0, "architecture_violations", return_value=[]) as scan:
                    with patch.object(d0, "read_tsv", side_effect=AssertionError("unexpected TSV read")):
                        with contextlib.redirect_stdout(output):
                            args.handler(args)
                scan.assert_called_once_with(self.repo.resolve())
                self.assertIn("D0_VERIFY=PASS ARCH_VIOLATIONS=0", output.getvalue())
                for rule in d0.ARCHITECTURE_RULES:
                    self.assertIn(rule.upper().replace("-", "_") + "=0", output.getvalue())

    def test_architecture_still_rejects_real_source_violation_without_manifests(self):
        source = self.repo / "app/src/main/kotlin/com/ghostlock/app/core/Probe.kt"
        source.parent.mkdir(parents=True)
        source.write_text(
            "package com.ghostlock.app.core\nimport android.content.Context\n",
            encoding="utf-8",
        )
        for fixtures in (None, self.fixtures):
            with self.subTest(fixtures=fixtures):
                with self.assertRaisesRegex(ValueError, "architecture violations") as error:
                    d0.verify_inventory(self.repo, fixtures, "architecture")
                self.assertIn(
                    "core-outer-dependency|app/src/main/kotlin/com/ghostlock/app/core/Probe.kt:2|",
                    str(error.exception),
                )

    def test_manifest_sections_still_require_fixture_argument(self):
        for section in ("all", "inventory", "component", "ksu", "surface"):
            with self.subTest(section=section):
                args = d0.parser().parse_args([
                    "verify", "--repo", str(self.repo), "--section", section,
                ])
                with self.assertRaisesRegex(ValueError, "--fixtures is required"):
                    args.handler(args)

    def test_log_channel_capacity_checks_semantics_not_argument_spelling(self):
        source = self.repo / "app/src/main/kotlin/com/ghostlock/app/core/log/KotlinLogConnector.kt"
        source.parent.mkdir(parents=True)
        cases = [
            ("Channel<String>(Channel.UNLIMITED)", False),
            ("Channel<String>(capacity = Channel.UNLIMITED)", False),
            ("Channel<String>(\n    capacity =\n        Channel.UNLIMITED,\n)", False),
            ("Channel<String>(16)", True),
            ("Channel<String>(capacity = 16)", True),
            ("Channel<String>(capacity = Channel.BUFFERED)", True),
        ]
        for expression, bounded in cases:
            with self.subTest(expression=expression):
                source.write_text(
                    "package com.ghostlock.app.core.log\n"
                    "import kotlinx.coroutines.channels.Channel\n"
                    f"private val input = {expression}\n",
                    encoding="utf-8",
                )
                violations = [
                    violation for violation in d0.architecture_violations(self.repo)
                    if "|bounded-log-channel-" in violation
                ]
                self.assertEqual(1 if bounded else 0, len(violations), violations)

    def test_manifest_sections_still_require_all_manifests(self):
        expected = "missing manifests: " + ",".join(d0.TSV_NAMES)
        for section in ("all", "inventory", "component", "ksu", "surface"):
            with self.subTest(section=section):
                with self.assertRaises(ValueError) as error:
                    d0.verify_inventory(self.repo, self.fixtures, section)
                self.assertEqual(expected, str(error.exception))
        with self.assertRaises(ValueError) as error:
            d0.verify_inventory(self.repo, self.fixtures)
        self.assertEqual(expected, str(error.exception))

    def test_existing_manifest_content_validation_is_preserved(self):
        self.fixtures.mkdir()
        for name in d0.TSV_NAMES:
            (self.fixtures / name).write_text("action\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "surface counts=0/0/0"):
            d0.verify_inventory(self.repo, self.fixtures, "surface")

    def test_bxp3_catalog_projection_allowlist_is_exact_and_handles_first_import(self):
        coordinator = self.repo / (
            "app/src/main/kotlin/com/ghostlock/app/composition/plugin/"
            "PluginCatalogCoordinator.kt"
        )
        coordinator.parent.mkdir(parents=True)
        coordinator.write_text(
            "package com.ghostlock.app.composition.plugin\n\n"
            "import com.ghostlock.app.composition.backend.BackendCatalogProjection\n"
            "import com.ghostlock.app.composition.backend.BackendCatalogView\n",
            encoding="utf-8",
        )
        probe = self.repo / "app/src/main/kotlin/com/ghostlock/app/data/Probe.kt"
        probe.parent.mkdir(parents=True)
        probe.write_text(
            "package com.ghostlock.app.data\n\n"
            "import com.ghostlock.app.composition.backend.BackendCatalogView\n",
            encoding="utf-8",
        )

        violations = [
            item for item in d0.architecture_violations(self.repo)
            if item.startswith("backend-composition-boundary|")
        ]
        self.assertFalse(any("PluginCatalogCoordinator.kt" in item for item in violations), violations)
        self.assertTrue(any("data/Probe.kt" in item for item in violations), violations)

    def test_bxp3_plugin_loader_is_the_only_reflection_exception(self):
        loader = self.repo / (
            "app/src/main/kotlin/com/ghostlock/app/data/runhost/plugin/RunHostPluginLoader.kt"
        )
        loader.parent.mkdir(parents=True)
        loader.write_text(
            "package com.ghostlock.app.data.runhost.plugin\n"
            "import java.lang.reflect.Modifier\n"
            "private fun load(name: String, parent: ClassLoader) = "
            "Class.forName(name, false, parent)\n",
            encoding="utf-8",
        )
        probe = loader.parent / "ReflectionProbe.kt"
        probe.write_text(
            "package com.ghostlock.app.data.runhost.plugin\n"
            "private fun load(name: String) = Class.forName(name)\n",
            encoding="utf-8",
        )

        violations = [
            item for item in d0.architecture_violations(self.repo)
            if item.startswith("runtime-reflection|")
        ]
        self.assertFalse(any("RunHostPluginLoader.kt" in item for item in violations), violations)
        self.assertTrue(any("ReflectionProbe.kt" in item for item in violations), violations)

    def test_bxp3_plugin_owner_guard_rejects_host_native_and_plugin_reverse_edges(self):
        app_build = self.repo / "app/build.gradle.kts"
        app_build.parent.mkdir(parents=True)
        app_build.write_text(
            'dependencies { implementation("net.java.dev.jna:jna:5.19.1") }\n'
            "android { externalNativeBuild { } }\n",
            encoding="utf-8",
        )
        plugin_source = self.repo / "plugin/packages/probe/src/main/kotlin/Probe.kt"
        plugin_source.parent.mkdir(parents=True)
        plugin_source.write_text(
            "package probe\nimport com.ghostlock.app.AppContainer\n",
            encoding="utf-8",
        )
        retired = self.repo / (
            "app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendRegistry.kt"
        )
        retired.parent.mkdir(parents=True)
        retired.write_text(
            "package com.ghostlock.app.composition.backend\nobject BackendRegistry\n",
            encoding="utf-8",
        )

        violations = [
            item for item in d0.architecture_violations(self.repo)
            if item.startswith("plugin-owner-boundary|")
        ]
        self.assertTrue(any("host-jna-dependency" in item for item in violations), violations)
        self.assertTrue(any("host-native-build" in item for item in violations), violations)
        self.assertTrue(any("plugin-to-host-edge" in item for item in violations), violations)
        self.assertTrue(any("retired-host-plugin-owner" in item for item in violations), violations)

    def test_bxp3_file_actions_supersede_old_batches_without_copying_old_ledger(self):
        manifest = self.repo / "app/src/main/AndroidManifest.xml"
        manifest.parent.mkdir(parents=True)
        manifest.write_text("<manifest />\n", encoding="utf-8")
        present = {
            "app/src/main/kotlin/com/ghostlock/app/data/plugin/AndroidPluginArchiveVerifier.kt":
                "package com.ghostlock.app.data.plugin\nclass AndroidPluginArchiveVerifier\n",
            "app/src/main/kotlin/com/ghostlock/app/runhost/appuid/AppUidRunHostService.kt":
                "package com.ghostlock.app.runhost.appuid\nclass AppUidRunHostService\n",
            "app/src/test/kotlin/com/ghostlock/app/composition/backend/BackendCatalogProjectionTest.kt":
                "package com.ghostlock.app.composition.backend\nclass BackendCatalogProjectionTest\n",
        }
        for relative, content in present.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

        rows = {row["path"]: row for row in d0.file_action_rows(self.repo)}
        self.assertEqual(
            ("added-approved", d0.BXP3_B1_BATCH),
            (
                rows["app/src/main/kotlin/com/ghostlock/app/data/plugin/AndroidPluginArchiveVerifier.kt"]["action"],
                rows["app/src/main/kotlin/com/ghostlock/app/data/plugin/AndroidPluginArchiveVerifier.kt"]["batch"],
            ),
        )
        self.assertEqual(
            ("modify-in-place", d0.BXP3_B1_BATCH),
            (
                rows["app/src/main/kotlin/com/ghostlock/app/runhost/appuid/AppUidRunHostService.kt"]["action"],
                rows["app/src/main/kotlin/com/ghostlock/app/runhost/appuid/AppUidRunHostService.kt"]["batch"],
            ),
        )
        self.assertEqual(
            ("move-target-1to1", d0.BXP3_B1_BATCH),
            (
                rows["app/src/test/kotlin/com/ghostlock/app/composition/backend/BackendCatalogProjectionTest.kt"]["action"],
                rows["app/src/test/kotlin/com/ghostlock/app/composition/backend/BackendCatalogProjectionTest.kt"]["batch"],
            ),
        )
        self.assertEqual(
            ("delete", "ABSENT", d0.BXP3_B1_BATCH),
            (
                rows["app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendDefinition.kt"]["action"],
                rows["app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendDefinition.kt"]["source_state"],
                rows["app/src/main/kotlin/com/ghostlock/app/composition/backend/BackendDefinition.kt"]["batch"],
            ),
        )


if __name__ == "__main__":
    unittest.main()
