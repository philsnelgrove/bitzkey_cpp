# Build Release, run the tests, then package dist\Bitzkey-Setup-<version>.exe with Inno Setup 6.
param([string]$Version = "1.0.0")
Set-Location $PSScriptRoot

& .\build.ps1
if ($LASTEXITCODE -ne 0) { throw "The build or its tests failed." }

$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 isn't installed (winget install JRSoftware.InnoSetup)." }

& $iscc "/DAppVersion=$Version" packaging\bitzkey.iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed." }
Write-Host "Built dist\Bitzkey-Setup-$Version.exe"
