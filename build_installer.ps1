# Build Release, run the tests, then package dist\Bitzkey-Setup-<version>.exe with Inno Setup 6.
# The version comes from the VERSION file, unless -Version gives another.
param([string]$Version = "")
Set-Location $PSScriptRoot
if (-not $Version) { $Version = (Get-Content VERSION -TotalCount 1).Trim() }

& .\build.ps1
if ($LASTEXITCODE -ne 0) { throw "The build or its tests failed." }

# The manager, built from the bitzkey-manager checkout beside this one
& ..\bitzkey-manager\build_exe.ps1 -Icon (Join-Path $PSScriptRoot "packaging\bitzkey.ico")
if (-not $? -or -not (Test-Path ..\bitzkey-manager\dist\bitzkey_manager.exe)) { throw "The manager failed to build." }
Set-Location $PSScriptRoot

$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 isn't installed (winget install JRSoftware.InnoSetup)." }

& $iscc "/DAppVersion=$Version" packaging\bitzkey.iss
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed." }
Write-Host "Built dist\Bitzkey-Setup-$Version.exe"
