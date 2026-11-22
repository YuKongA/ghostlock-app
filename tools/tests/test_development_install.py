"""Installer regression tests: no package installs, task writes, or network calls."""
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('development_install', ROOT / 'tools/engineering/install/install.py')
INSTALL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALL)


class DevelopmentInstallTest(unittest.TestCase):
    def test_repository_versions(self):
        result = INSTALL.requirements()
        self.assertEqual(result['daemon'], '25')
        self.assertEqual(result['jdk'], '21')
        self.assertIn('platforms;android-37.2', result['sdk'])
        self.assertIn('ndk;30.0.16248370', result['sdk'])

    def test_planning_does_not_read_android_or_rust(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(INSTALL.requirements(Path(tmp), 'planning'), {})

    def test_sdk_archive_selects_mac_architecture(self):
        xml = '<repository><remotePackage path="cmdline-tools;latest"><archives>'
        for arch in ['x64', 'aarch64']:
            xml += ('<archive><host-os>macosx</host-os><host-arch>' + arch + '</host-arch>'
                    '<complete><url>' + arch + '.zip</url><checksum type="sha1">abc</checksum>'
                    '</complete></archive>')
        xml += '</archives></remotePackage></repository>'
        self.assertEqual(INSTALL.sdk_archive(xml, 'mac', 'aarch64'),
                         (INSTALL.GOOGLE + 'aarch64.zip', 'abc', 'sha1'))
        with self.assertRaises(ValueError):
            INSTALL.sdk_archive(xml, 'linux', 'x64')

    def test_local_properties_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            local = root / 'local.properties'
            content = '# comment\nother=value\nsdk.dir=/tmp/Android\\ SDK\n'
            local.write_text(content)
            self.assertEqual(INSTALL.sdk_location(root, root / 'default'), Path('/tmp/Android SDK'))
            self.assertEqual(local.read_text(), content)

    def test_checksum_failure_removes_download(self):
        with tempfile.TemporaryDirectory() as tmp:
            destination = Path(tmp) / 'download'
            with patch.object(INSTALL.urllib.request, 'urlopen', return_value=io.BytesIO(b'wrong')):
                with self.assertRaises(ValueError):
                    INSTALL.checked_download('https://example.invalid/tool', '0' * 64, destination)
            self.assertFalse(destination.exists())

    def test_download_uses_identifiable_user_agent(self):
        with patch.object(INSTALL.urllib.request, 'urlopen', return_value=io.BytesIO(b'{}')) as request:
            self.assertEqual(INSTALL.get('https://example.invalid/metadata'), b'{}')
            self.assertEqual(request.call_args.args[0].get_header('User-agent'),
                             'ghostlock-development-installer/1')

    def test_existing_archive_directory_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            home = Path(tmp)
            target = home / 'jdk-test'
            target.mkdir()
            marker = target / '.ghostlock-archive-sha256'
            marker.write_text('verified')
            with patch.object(INSTALL, 'checked_download'), \
                 patch.object(INSTALL.subprocess, 'check_output', return_value='jdk-test/bin/java\n'), \
                 patch.object(INSTALL.subprocess, 'run', side_effect=AssertionError('overwrite')):
                self.assertEqual(INSTALL.archive('https://example.invalid/jdk.tar.gz', 'verified', home), target)
                marker.unlink()
                with self.assertRaises(ValueError):
                    INSTALL.archive('https://example.invalid/jdk.tar.gz', 'verified', home)
            self.assertTrue(target.is_dir())

    def test_linux_arm_android_rejected_without_network(self):
        import argparse
        with patch.object(INSTALL.platform, 'system', return_value='Linux'), \
             patch.object(INSTALL.platform, 'machine', return_value='aarch64'), \
             patch.object(INSTALL, 'get', side_effect=AssertionError('network')):
            with self.assertRaises(ValueError):
                INSTALL.install(argparse.Namespace(profile='android', plan=False,
                                                   system_image=None, accept_licenses=False))

    def test_shell_plan_no_package_manager(self):
        result = subprocess.run(['bash', str(ROOT / 'tools/engineering/install/install.sh'),
                                 '--plan', '--profile', 'planning'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"requirements": {}', result.stdout)
        self.assertNotIn('brew install', result.stdout)

    def test_invalid_options_fail_before_install(self):
        for args in [['--unknown'], ['--profile=planning'], ['--profile'],
                     ['--profile', 'planning', '--accept-licenses'], ['--system-image', 'wrong']]:
            result = subprocess.run(['bash', str(ROOT / 'tools/engineering/install/install.sh'), *args],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, (args, result.stdout, result.stderr))

    def test_cli_plan_does_not_call_network_or_write(self):
        import argparse
        with patch.object(INSTALL, 'get', side_effect=AssertionError('network')), \
             patch.object(Path, 'mkdir', side_effect=AssertionError('write')):
            INSTALL.install(argparse.Namespace(profile='planning', plan=True,
                                               system_image=None, accept_licenses=False))


if __name__ == '__main__':
    unittest.main()
