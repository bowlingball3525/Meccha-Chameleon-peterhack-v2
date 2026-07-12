@echo off
setlocal

rem Build peterhack + camo bridge into one folder on the Desktop.
rem Usage: build.bat [rebuild]   (pass "rebuild" to force a clean rebuild)

set "SOLUTION=%~dp0peterhack.slnx"
set "CONFIG=Release"
set "PLATFORM=x64"
set "DEPLOY=%USERPROFILE%\Desktop\peterhack"

set "TARGET=Build"
if /i "%~1"=="rebuild" set "TARGET=Rebuild"

rem Locate MSBuild via vswhere (shipped with VS 2017+).
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [error] vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do (
    set "MSBUILD=%%i"
)

if not defined MSBUILD (
    echo [error] MSBuild.exe not found via vswhere.
    exit /b 1
)

if not exist "%DEPLOY%" mkdir "%DEPLOY%"

echo Using MSBuild: %MSBUILD%
echo Target: %TARGET%  Config: %CONFIG%^|%PLATFORM%
echo Deploy folder: %DEPLOY%
echo.

"%MSBUILD%" "%SOLUTION%" /t:%TARGET% /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /m /nologo /v:minimal
if errorlevel 1 (
    echo.
    echo [error] peterhack build failed.
    exit /b 1
)

echo.
echo Building camo bridge...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0runtime\scripts\build-camo-bridge.ps1" -DeployDir "%DEPLOY%"
if errorlevel 1 (
    echo.
    echo [error] Camo bridge build failed.
    exit /b 1
)

echo.
echo [ok] Build succeeded. Output folder:
echo   %DEPLOY%
echo     peterhack-loader.exe
echo     manifest.json
echo     peterhack.dll
echo     bridge\meccha-xenos-bridge.dll
echo     bridge\mesh-profiles\
echo.
echo Run: %DEPLOY%\peterhack-loader.exe --local --wait
endlocal
