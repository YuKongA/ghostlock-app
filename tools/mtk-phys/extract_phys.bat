@echo off
rem Run on your PC (Windows cmd): push mtk-phys.sh to the device and run it as
rem root over adb. Prints kernel_phys_load / kernel_phys_offset.
rem
rem   set ANDROID_SERIAL=<serial>
rem   tools\mtk-phys\extract_phys.bat
setlocal

set "ADB=adb"
if not "%ANDROID_SERIAL%"=="" set "ADB=adb -s %ANDROID_SERIAL%"

%ADB% get-state >nul 2>&1
if errorlevel 1 (
  echo error: no adb device 1>&2
  exit /b 1
)

%ADB% root >nul 2>&1
%ADB% wait-for-device

set "DEV=/data/local/tmp/ghostlock-mtk-phys.sh"
%ADB% push "%~dp0mtk-phys.sh" "%DEV%" >nul

set "UID="
for /f "usebackq delims=" %%u in (`%ADB% shell id -u`) do set "UID=%%u"
if "%UID%"=="0" (
  %ADB% shell sh %DEV%
) else (
  %ADB% shell "su -c 'sh %DEV%'"
)

endlocal
