@echo off
REM ============================================================================
REM  scripts/build_windows.bat -- build the plug-ins and attach their PiPL
REM  resources in one step.
REM
REM  Run from a "Developer Command Prompt for VS", with AE_SDK_ROOT pointing at
REM  an unzipped After Effects SDK. See docs/BUILDING.md for what the two
REM  resource tools do and why the second one is not optional.
REM ============================================================================
setlocal enabledelayedexpansion

if "%AE_SDK_ROOT%"=="" (
    echo AE_SDK_ROOT is not set. Example:
    echo     set AE_SDK_ROOT=C:\SDKs\AfterEffectsSDK
    exit /b 1
)

if not exist "%AE_SDK_ROOT%\Examples\Headers" (
    echo %AE_SDK_ROOT% does not look like an After Effects SDK.
    echo Expected: %AE_SDK_ROOT%\Examples\Headers
    exit /b 1
)

echo Building plug-ins...
make plugins AE_SDK_ROOT=%AE_SDK_ROOT%
if errorlevel 1 exit /b 1

echo.
echo Attaching PiPL resources...
if not exist build\res mkdir build\res

pushd resources\pipl
for %%R in (*.r) do (
    echo   %%~nR
    "%AE_SDK_ROOT%\Examples\Util\pipltool.exe" -sc "%%R" "..\..\build\res\%%~nR.rc" || exit /b 1
    rc /nologo /r /fo "..\..\build\res\%%~nR.res" "..\..\build\res\%%~nR.rc" || exit /b 1
)
popd

echo.
echo Done. Binaries are in build\plugins.
echo Attach the matching .res to each with the linker, or use link /dll /noentry
echo as shown in docs\BUILDING.md, then copy them into:
echo   C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\
endlocal
