#!/usr/bin/env python3
"""Shared user-local tool installation, after install.sh provisions Python and OS tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
GOOGLE = 'https://dl.google.com/android/repository/'


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "ghostlock-development-installer/1"}), timeout=120) as response:
        return response.read()


def match(pattern, text):
    result = re.search(pattern, text)
    if not result:
        raise ValueError('Repository version declaration changed: ' + pattern)
    return result.group(1)


def requirements(root=ROOT, profile="full"):
    if profile == "planning":
        return {}
    app = (root / 'app/build.gradle.kts').read_text()
    plugin = (root / 'plugin/packages/diagnose/build.gradle.kts').read_text()
    api = match(r'version\s*=\s*release\((\d+)\)', app)
    minor = match(r'minorApiLevel\s*=\s*(\d+)', app)
    ndk = match(r'ndkVersion\s*=\s*"([^"]+)"', plugin)
    if ndk != match(r'ndkVersion\s*=\s*"([^"]+)"', app):
        raise ValueError('App/plugin NDK declarations disagree')
    return {
        'daemon': match(r'toolchainVersion=(\d+)', (root / 'gradle/gradle-daemon-jvm.properties').read_text()),
        'jdk': match(r'jvmToolchain\((\d+)\)', app),
        'sdk': [f'platforms;android-{api}.{minor}',
                'build-tools;' + match(r'val androidBuildToolsVersion\s*=\s*"([^"]+)"', plugin),
                'ndk;' + ndk, 'cmake;3.22.1', 'platform-tools'],
        'rust': (match(r'rust-version\s*=\s*"([^"]+)"', (root / 'tools/extract_rs/Cargo.toml').read_text()) if profile == 'full' else None),
    }


def checked_download(url, checksum, destination, algorithm='sha256'):
    # Stream large JDK/Node archives rather than retaining them in memory.
    digest = hashlib.new(algorithm)
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "ghostlock-development-installer/1"}), timeout=120) as response, destination.open('wb') as out:
        while True:
            block = response.read(1024 * 1024)
            if not block:
                break
            digest.update(block)
            out.write(block)
    if digest.hexdigest().lower() != checksum.lower():
        destination.unlink()
        raise ValueError('Checksum mismatch: ' + url)


def archive(url, checksum, home):
    name = url.rsplit('/', 1)[-1]
    # Download/extract on the installation filesystem, then publish a complete directory.
    with tempfile.TemporaryDirectory(prefix='.install-', dir=home) as tmp:
        package = Path(tmp) / name
        checked_download(url, checksum, package)
        members = subprocess.check_output(['tar', '-tf', str(package)], text=True).splitlines()
        top = members[0].split('/')[0]
        if not top or top in {'.', '..'}:
            raise ValueError('Unexpected archive layout: ' + url)
        directory = home / top
        marker = directory / '.ghostlock-archive-sha256'
        if marker.is_file() and marker.read_text() == checksum:
            return directory
        if directory.exists():
            raise ValueError('Existing unverified installation preserved: ' + str(directory))
        stage = Path(tmp) / 'unpacked'
        stage.mkdir()
        subprocess.run(['tar', '-xf', str(package), '-C', str(stage)], check=True)
        (stage / top / '.ghostlock-archive-sha256').write_text(checksum)
        (stage / top).rename(directory)
        return directory


def sdk_archive(xml, host, arch):
    host = "macosx" if host == "mac" else host
    tree = ET.fromstring(xml)
    for package in tree.findall('remotePackage'):
        if package.get('path') != 'cmdline-tools;latest':
            continue
        for item in package.findall('./archives/archive'):
            if item.findtext('host-os') == host and item.findtext('host-arch') in (None, arch):
                part = item.find('complete')
                checksum = part.find('checksum')
                return GOOGLE + part.findtext('url'), checksum.text, checksum.get('type', 'sha1')
    raise ValueError('No Android command-line tools archive for ' + host)


def sdk_location(root, default):
    local = root / 'local.properties'
    if local.exists():
        for line in local.read_text().splitlines():
            if re.match(r'^\s*sdk\.dir\s*[=:]', line):
                value = re.split(r'[=:]', line, maxsplit=1)[1].strip()
                value = value.replace('\\:', ':').replace('\\ ', ' ').replace('\\\\', '\\')
                path = Path(value).expanduser()
                if not path.is_absolute():
                    raise ValueError('Existing sdk.dir must be an absolute host path')
                return path
    return default


def run(*args, **kwargs):
    print('+', shlex.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), check=True, **kwargs)


def install(args):
    if sys.version_info < (3, 9):
        raise ValueError('Python >=3.9 required; use a supported workstation release')
    req = requirements(profile=args.profile)
    host = {'Darwin': 'mac', 'Linux': 'linux'}.get(platform.system())
    arch = {'x86_64': 'x64', 'arm64': 'aarch64', 'aarch64': 'aarch64'}.get(platform.machine())
    if not host or not arch:
        raise ValueError('Supported hosts: macOS x64/arm64, Linux x64/arm64 (planning only on Linux arm64)')
    if host == 'linux' and arch != 'x64' and args.profile != 'planning':
        raise ValueError('Android Linux tools require x86_64')
    if args.profile == 'planning' and (args.system_image or args.accept_licenses):
        raise ValueError('Android options require android/full profile')
    if args.plan:
        print(json.dumps({'host': host, 'arch': arch, 'profile': args.profile, 'requirements': req}, indent=2))
        return
    home = Path.home() / '.local/share/ghostlock-development'
    sdk = sdk_location(ROOT, home / 'android-sdk') if args.profile != 'planning' else None
    if sdk and ((sdk / 'cmdline-tools/latest/bin/sdkmanager.bat').exists() or str(sdk).startswith('/mnt/')):
        raise ValueError('Use a Linux SDK, not a Windows SDK or /mnt drive, in WSL')
    home.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    exports = {}
    paths = []
    # Latest Node 24 LTS patch satisfies current transitive lockfile engines; package versions stay locked.
    node_arch = 'arm64' if arch == 'aarch64' else 'x64'
    node_host = 'darwin' if host == 'mac' else 'linux'
    base = 'https://nodejs.org/dist/latest-v24.x/'
    sums = get(base + 'SHASUMS256.txt').decode()
    checksum, filename = next(line.split() for line in sums.splitlines()
                              if line.endswith(f'-{node_host}-{node_arch}.tar.gz'))
    node_home = home / filename.removesuffix('.tar.gz')
    if not (node_home / 'bin/node').is_file():
        node_home = archive(base + filename, checksum, home)
    paths.append(str(node_home / 'bin'))
    if args.profile != 'planning':
        for major, variable in [(req['daemon'], 'JAVA_HOME'), (req['jdk'], 'JDK_TOOLCHAIN_HOME')]:
            url = (f'https://api.adoptium.net/v3/assets/latest/{major}/hotspot'
                   f'?architecture={arch}&image_type=jdk&os={host}&vendor=eclipse')
            package = json.loads(get(url))[0]['binary']['package']
            # Use the versioned archive directory; no global Java alternatives/default changes.
            cache = home / (package['name'] + '.location')
            directory = Path(cache.read_text()) if cache.is_file() else None
            marker = directory / '.ghostlock-archive-sha256' if directory else None
            if marker is None or not marker.is_file() or marker.read_text() != package['checksum']:
                directory = archive(package['link'], package['checksum'], home)
                cache.write_text(str(directory))
            java = directory / 'Contents/Home' if host == 'mac' else directory
            exports[variable] = str(java)
        paths.append(exports['JAVA_HOME'] + '/bin')
        gradle_home = home / 'gradle'
        gradle_home.mkdir(exist_ok=True)
        props = gradle_home / 'gradle.properties'
        if not props.exists():
            props.write_text('org.gradle.java.installations.fromEnv=JAVA_HOME,JDK_TOOLCHAIN_HOME\n')
        elif 'org.gradle.java.installations.fromEnv=JAVA_HOME,JDK_TOOLCHAIN_HOME' not in props.read_text().splitlines():
            raise ValueError('Managed Gradle properties changed; preserve it and review Java discovery before continuing')
        exports['GRADLE_USER_HOME'] = str(gradle_home)
        sdk.mkdir(parents=True, exist_ok=True)
        manager = sdk / 'cmdline-tools/latest/bin/sdkmanager'
        if not manager.is_file():
            destination = sdk / 'cmdline-tools/latest'
            if destination.exists():
                raise ValueError('Incomplete cmdline-tools/latest exists; inspect it rather than overwrite')
            url, checksum, algorithm = sdk_archive(get(GOOGLE + 'repository2-3.xml'), host, arch)
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix='.install-', dir=destination.parent) as tmp:
                download = Path(tmp) / 'sdk.zip'
                checked_download(url, checksum, download, algorithm)
                run('unzip', '-q', download, '-d', tmp)
                destination.parent.mkdir(exist_ok=True)
                (Path(tmp) / 'cmdline-tools').rename(destination)
        exports['ANDROID_HOME'] = str(sdk)
        paths += [str(sdk / 'platform-tools'), str(manager.parent), str(sdk / 'emulator')]
    env.update(exports)
    env['PATH'] = os.pathsep.join(paths + [env['PATH']])
    run('node', '--version', env=env)
    run('npm', '--prefix', ROOT / 'tools/engineering', 'ci', '--engine-strict', env=env)
    run('npm', '--prefix', ROOT / 'tools/engineering', 'run', '--silent', 'bd', '--', 'version', env=env)
    run('npm', '--prefix', ROOT / 'tools/engineering', 'run', '--silent', 'openspec', '--', '--version', env=env)
    if args.profile != 'planning':
        # Explicit opt-in only. Otherwise sdkmanager prompts for the user's license acceptance.
        if args.accept_licenses:
            subprocess.run([str(manager), '--sdk_root=' + str(sdk), '--licenses'],
                           input='y\n' * 200, text=True, env=env, check=True)
        else:
            run(manager, '--sdk_root=' + str(sdk), '--licenses', env=env)
        packages = req['sdk'] + (['emulator', args.system_image] if args.system_image else [])
        run(manager, '--sdk_root=' + str(sdk), *packages, env=env)
        for command in [('java', '-version'), ('cmake', '--version'), ('ctest', '--version'), ('ninja', '--version')]:
            run(*command, env=env)
        cmake_version = subprocess.check_output(['cmake', '--version'], env=env, text=True)
        version = tuple(map(int, re.search(r'(\d+)\.(\d+)\.(\d+)', cmake_version).groups()))
        if version < (3, 22, 0):
            raise ValueError('Host CMake must be >=3.22; use a supported workstation release')
    if args.profile == 'full':
        triple = arch.replace('x64', 'x86_64') + ('-apple-darwin' if host == 'mac' else '-unknown-linux-gnu')
        url = 'https://static.rust-lang.org/rustup/dist/' + triple + '/rustup-init'
        with tempfile.TemporaryDirectory(prefix='gl-rustup-') as tmp:
            executable = Path(tmp) / 'rustup-init'
            checked_download(url, get(url + '.sha256').decode().split()[0], executable)
            executable.chmod(0o755)
            run(executable, '-y', '--no-modify-path', '--default-toolchain', 'none', env=env)
        cargo_bin = str(Path(env.get('CARGO_HOME', str(Path.home() / '.cargo'))) / 'bin')
        paths.append(cargo_bin)
        env['PATH'] = cargo_bin + os.pathsep + env['PATH']
        run('rustup', 'toolchain', 'install', req['rust'], '--profile', 'minimal', env=env)
        exports['RUSTUP_TOOLCHAIN'] = req['rust']
        for key in ('CARGO_HOME', 'RUSTUP_HOME'):
            if key in env:
                exports[key] = env[key]
    activation = ROOT / 'build/engineering' / ('env-' + args.profile + '.sh')
    activation.parent.mkdir(parents=True, exist_ok=True)
    activation.write_text('# Generated environment; source in the shell used for this checkout.\n' +
                          ''.join(f'export {k}={shlex.quote(v)}\n' for k, v in exports.items()) +
                          'export PATH=' + shlex.quote(os.pathsep.join(paths)) + ':"$PATH"\n')
    print('ENVIRONMENT_INSTALLED; no task bootstrap, product tests or device gates executed.')
    print('Activate: source ' + shlex.quote(str(activation)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=['planning', 'android', 'full'], default='android')
    parser.add_argument('--accept-licenses', action='store_true')
    parser.add_argument('--system-image', help='Exact SDK system-images;... package; installs emulator too, does not create/launch AVD')
    parser.add_argument('--plan', action='store_true', help='Read repository requirements only; no installation/network/write')
    args = parser.parse_args()
    if args.system_image and not args.system_image.startswith('system-images;'):
        parser.error('--system-image must be an exact system-images;... SDK package')
    install(args)


if __name__ == '__main__':
    main()
