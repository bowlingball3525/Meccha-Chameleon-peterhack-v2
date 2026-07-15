# Refresh bridge mesh-profiles in the Desktop deploy folder.
# Run from repo root: powershell -File scripts\setup-camo-bridge.ps1

$root = Split-Path -Parent $PSScriptRoot
$deploy = Join-Path $env:USERPROFILE "Desktop\peterhack"
$bridgeDir = Join-Path $deploy "bridge"
$srcProfiles = Join-Path $root "runtime\resources\mesh-profiles"
$dstProfiles = Join-Path $bridgeDir "mesh-profiles"

if (-not (Test-Path (Join-Path $bridgeDir "peterhack-bridge.dll"))) {
    Write-Host "Missing bridge DLL. Build first:" -ForegroundColor Yellow
    Write-Host "  $root\build.bat" -ForegroundColor Yellow
    exit 1
}

New-Item -ItemType Directory -Force -Path $dstProfiles | Out-Null
Copy-Item -Force (Join-Path $srcProfiles "*.json") $dstProfiles
Write-Host "Refreshed bridge mesh-profiles in $bridgeDir" -ForegroundColor Green
