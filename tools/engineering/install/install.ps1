# Windows built-ins only; the existing POSIX build/report chain runs inside WSL2.
param(
    [ValidateSet('planning', 'android', 'full')][string]$Profile = 'android',
    [switch]$AcceptLicenses,
    [string]$SystemImage = ''
)
$ErrorActionPreference = 'Stop'
$Distro = 'Ubuntu-24.04'
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'Windows 11 with WSL support is required. Enable virtualization and Windows updates first.'
}
$distros = (& wsl.exe --list --quiet 2>$null) -replace "`0", ''
if ($LASTEXITCODE -ne 0 -or $distros -notcontains $Distro) {
    & wsl.exe --install --distribution $Distro --no-launch
    if ($LASTEXITCODE -ne 0) { throw "WSL installation exit $LASTEXITCODE; rerun in an elevated PowerShell." }
    Write-Host "If Windows requests a restart, restart first. Run 'wsl -d $Distro' once to create your Linux user, then rerun this installer."
    exit 3
}
$script = Join-Path $PSScriptRoot 'install.sh'
$linuxScript = & wsl.exe -d $Distro -- wslpath -a -u $script
if ($LASTEXITCODE -ne 0) { throw 'Initialize the Ubuntu user with wsl -d Ubuntu-24.04, then rerun.' }
$argsLinux = @('-d', $Distro, '--', 'bash', $linuxScript.Trim(), '--profile', $Profile)
if ($AcceptLicenses) { $argsLinux += '--accept-licenses' }
if ($SystemImage) { $argsLinux += @('--system-image', $SystemImage) }
& wsl.exe @argsLinux
if ($LASTEXITCODE -ne 0) { throw "Linux installer exited $LASTEXITCODE" }
Write-Host 'Environment installed in your WSL user home. Use the printed source command inside WSL, not Windows PowerShell.'
