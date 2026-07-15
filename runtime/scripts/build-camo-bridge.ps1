# Build peterhack camouflage bridge (camo-only).
# Run from repo root: powershell -File runtime\scripts\build-camo-bridge.ps1

param(
    [string]$RuntimeRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$DeployDir = (Join-Path $env:USERPROFILE "Desktop\peterhack")
)

$ErrorActionPreference = "Stop"
$DeployDir = [System.IO.Path]::GetFullPath($DeployDir)
$OutDir = Join-Path $RuntimeRoot ".build\bin"
$ObjDir = Join-Path $RuntimeRoot ".build\obj"
New-Item -ItemType Directory -Force -Path $OutDir, $ObjDir, $DeployDir | Out-Null

$BridgeSource = Join-Path $RuntimeRoot "src\bridge.cpp"
if (-not (Test-Path $BridgeSource)) {
    throw "Bridge source not found: $BridgeSource"
}

function Quote-CmdArg([string]$Value) {
    if ($Value -match '^[A-Za-z0-9_./:=+\-\\]+$') { return $Value }
    return '"' + ($Value -replace '"', '\"') + '"'
}

function Get-VsDevCmd {
    $VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VsWhere)) { return "" }
    $VsInstall = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $VsInstall) { return "" }
    $VsDevCmd = Join-Path $VsInstall "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $VsDevCmd) { return $VsDevCmd }
    return ""
}

function Invoke-VsToolCommand {
    param(
        [Parameter(Mandatory = $true)][string]$ToolName,
        [Parameter(Mandatory = $true)][string[]]$ToolArgs
    )
    if (Get-Command $ToolName -ErrorAction SilentlyContinue) {
        & $ToolName @ToolArgs
        if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE" }
        return
    }
    $VsDevCmd = Get-VsDevCmd
    if (-not $VsDevCmd) {
        throw "$ToolName was not found. Install Visual Studio Build Tools or use a Developer PowerShell."
    }
    $ArgText = ($ToolArgs | ForEach-Object { Quote-CmdArg $_ }) -join " "
    $CommandLine = "$(Quote-CmdArg $VsDevCmd) -arch=x64 -host_arch=x64 >nul && $ToolName $ArgText"
    cmd /d /c $CommandLine
    if ($LASTEXITCODE -ne 0) { throw "$ToolName failed with exit code $LASTEXITCODE" }
}

Push-Location $RuntimeRoot
try {
    $BridgeOutput = Join-Path $OutDir "peterhack-bridge.dll"
    Invoke-VsToolCommand -ToolName "cl.exe" -ToolArgs @(
        "/nologo", "/std:c++17", "/EHsc", "/O2", "/LD", $BridgeSource,
        "/I", (Join-Path $RuntimeRoot "include"),
        "/Fo:$(Join-Path $ObjDir 'bridge.obj')",
        "/Fe:$BridgeOutput",
        "Ws2_32.lib", "User32.lib", "Gdi32.lib"
    )

    if (-not (Test-Path $BridgeOutput)) { throw "Bridge DLL was not produced: $BridgeOutput" }

    $BridgeDeploy = Join-Path $DeployDir "bridge"
    New-Item -ItemType Directory -Force -Path $BridgeDeploy | Out-Null
    Copy-Item -Force $BridgeOutput (Join-Path $BridgeDeploy "peterhack-bridge.dll")

    $ProfilesSrc = Join-Path $RuntimeRoot "resources\mesh-profiles"
    $ProfilesDst = Join-Path $BridgeDeploy "mesh-profiles"
    New-Item -ItemType Directory -Force -Path $ProfilesDst | Out-Null
    Copy-Item -Force (Join-Path $ProfilesSrc "*.json") $ProfilesDst

    Write-Host "Built and deployed camo bridge to $BridgeDeploy" -ForegroundColor Green
}
finally {
    Pop-Location
}
