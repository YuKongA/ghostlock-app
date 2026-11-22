#!/usr/bin/env bash
# Bootstrap only OS prerequisites; install.py owns repository-derived tool versions.
set -euo pipefail
profile=android
plan=false
accept=false
image=''
args=("$@")
if [[ $# -eq 0 ]]; then args=(--profile android); fi
while [[ $# -gt 0 ]]; do
  case "$1" in
    --help|-h) echo 'Usage: bash install.sh [--profile planning|android|full] [--plan] [--accept-licenses] [--system-image SDK_PACKAGE]'; exit 0 ;;
    --profile) [[ $# -ge 2 ]] || exit 2; profile=$2; shift 2 ;;
    --system-image) [[ $# -ge 2 && $2 == 'system-images;'* ]] || exit 2; image=$2; shift 2 ;;
    --accept-licenses) accept=true; shift ;;
    --plan) plan=true; shift ;;
    *) echo "Unknown option: $1" >&2; exit 2 ;;
  esac
done
case "$profile" in planning|android|full) ;; *) echo 'Unknown profile' >&2; exit 2;; esac
if [[ "$profile" == planning && ( "$accept" == true || -n "$image" ) ]]; then
  echo 'Android options require android/full profile.' >&2; exit 2
fi
root=$(cd "$(dirname "$0")/../../.." && pwd)
if [[ "$plan" == true ]]; then
  command -v python3 >/dev/null || { echo 'Read-only plan needs Python3; see the documented package table. No packages installed.'; exit 2; }
  exec python3 "$root/tools/engineering/install/install.py" "${args[@]}"
fi
if [[ "$profile" != planning && $(uname -s) == Linux && -f "$root/local.properties" ]] && grep -Eq '^sdk\.dir[[:space:]]*=[[:space:]]*([A-Za-z](:|\\:)|/mnt/)'  "$root/local.properties"; then
  echo 'This checkout uses a Windows SDK. Use a Linux checkout/SDK in WSL; local.properties is preserved.' >&2; exit 2
fi
os=$(uname -s)
if [[ "$os" == Linux && "$profile" != planning && $(uname -m) != x86_64 ]]; then
  echo 'Android Linux host binaries require x86_64; use --profile planning on this host.' >&2; exit 2
fi
if [[ "$os" == Darwin ]]; then
  if ! command -v brew >/dev/null; then
    for candidate in /opt/homebrew/bin/brew /usr/local/bin/brew; do
      if [[ -x "$candidate" ]]; then eval "$("$candidate" shellenv)"; break; fi
    done
  fi
  command -v brew >/dev/null || { echo 'Homebrew is the macOS prerequisite.' >&2; exit 2; }
  if ! xcode-select -p >/dev/null 2>&1; then
    xcode-select --install
    echo 'Finish the Apple Command Line Tools dialog, then rerun this command.'; exit 3
  fi
  packages=(git ripgrep python ca-certificates)
  [[ "$profile" == planning ]] || packages+=(cmake ninja)
  brew install "${packages[@]}"
  export PATH="$(brew --prefix)/bin:$PATH"
elif [[ "$os" == Linux ]]; then
  elevate=()
  if [[ $EUID != 0 ]]; then elevate=(sudo); fi
  if command -v apt-get >/dev/null; then
    packages=(git ripgrep python3 ca-certificates curl tar xz-utils unzip)
    [[ "$profile" == planning ]] || packages+=(build-essential cmake ninja-build pkg-config)
    "${elevate[@]}" apt-get update
    "${elevate[@]}" apt-get install -y "${packages[@]}"
  elif command -v dnf >/dev/null; then
    packages=(git ripgrep python3 ca-certificates curl tar xz unzip)
    [[ "$profile" == planning ]] || packages+=(gcc gcc-c++ make cmake ninja-build pkgconf-pkg-config)
    "${elevate[@]}" dnf install -y "${packages[@]}"
  elif command -v zypper >/dev/null; then
    packages=(git ripgrep python3 ca-certificates curl tar xz unzip)
    [[ "$profile" == planning ]] || packages+=(gcc gcc-c++ make cmake ninja pkg-config)
    "${elevate[@]}" zypper --non-interactive install "${packages[@]}"
  elif command -v pacman >/dev/null; then
    packages=(git ripgrep python ca-certificates curl tar xz unzip)
    [[ "$profile" == planning ]] || packages+=(base-devel cmake ninja pkgconf)
    # No partial Arch upgrade: use the installed, current workstation repositories.
    "${elevate[@]}" pacman -S --needed --noconfirm "${packages[@]}"
  else
    echo 'Supported package managers: apt-get, dnf, zypper, pacman.' >&2; exit 2
  fi
else
  echo 'Use install.ps1 for Windows (WSL2), install.sh for macOS/Linux.' >&2; exit 2
fi
exec python3 "$root/tools/engineering/install/install.py" "${args[@]}"
