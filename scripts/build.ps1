<#
.SYNOPSIS
    Build 7-zip targets using NMAKE with Visual Studio toolchain.
.EXAMPLE
    .\build.ps1 -Target Fm
    .\build.ps1 -Target Alone2 -Platform x64
    .\build.ps1 -Target FileManager -Clean
    .\build.ps1 -Target Fm -Launch
#>
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet(
        # Bundles (self-contained)
        'Alone',          # 7za.exe  - console, many codecs embedded
        'Alone2',         # 7zz.exe  - console, all codecs embedded
        'Alone7z',        # 7zr.exe  - console, restricted codecs
        'Fm',             # 7zFM.exe - file manager, codecs embedded
        'Format7z',       # 7z.dll format plugin
        'Format7zF',      # 7z.dll format plugin (full)
        'Format7zR',      # 7zr.dll format plugin
        'Format7zExtract',
        'Format7zExtractR',
        'LzmaCon',        # lzma.exe
        'SFXCon',         # 7zCon.sfx
        'SFXSetup',       # 7zS.sfx
        'SFXWin',         # 7z.sfx
        # UI (requires external 7z.dll)
        'Client7z',       # Client7z.exe
        'Console',        # 7z.exe
        'Explorer',       # 7-zip.dll shell extension
        'Far',            # Far manager plugin
        'FileManager',    # 7zFM.exe
        'GUI'             # 7zG.exe
    )]
    [string]$Target = 'Fm',

    [ValidateSet('x64', 'x86', 'arm64', 'arm')]
    [string]$Platform = 'x64',

    [switch]$Clean,

    [switch]$Launch
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent

# Map target name to relative makefile directory
$bundleTargets = @('Alone', 'Alone2', 'Alone7z', 'Fm', 'Format7z', 'Format7zF',
                   'Format7zR', 'Format7zExtract', 'Format7zExtractR', 'LzmaCon',
                   'SFXCon', 'SFXSetup', 'SFXWin')
$uiTargets     = @('Client7z', 'Console', 'Explorer', 'Far', 'FileManager', 'GUI')

if ($bundleTargets -contains $Target) {
    $makeDir = Join-Path $repoRoot "CPP\7zip\Bundles\$Target"
} else {
    $makeDir = Join-Path $repoRoot "CPP\7zip\UI\$Target"
}

if (-not (Test-Path $makeDir)) {
    Write-Error "Target directory not found: $makeDir"
    exit 1
}

# Find Visual Studio via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found. Install Visual Studio Build Tools."
    exit 1
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    Write-Error "No Visual Studio installation with VC++ tools found."
    exit 1
}

$vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
if (-not (Test-Path $vcvarsall)) {
    Write-Error "vcvarsall.bat not found at: $vcvarsall"
    exit 1
}

# Map -Platform to vcvarsall architecture argument
$vcArch = switch ($Platform) {
    'x64'   { 'x64' }
    'x86'   { 'x86' }
    'arm64' { 'x64_arm64' }
    'arm'   { 'x64_arm' }
}

$nmakeTarget = if ($Clean) { 'clean' } else { '' }
$nmakeArgs   = "PLATFORM=$Platform"

Write-Host "Target:   $Target ($makeDir)"
Write-Host "Platform: $Platform (vcvarsall arch: $vcArch)"
if ($Clean) { Write-Host "Action:   clean" }
Write-Host ""

# Run vcvarsall + nmake inside cmd so the VS env is active for the build
$cmd = @"
call "$vcvarsall" $vcArch && cd /d "$makeDir" && nmake -nologo $nmakeArgs $nmakeTarget
"@

cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Launch) {
    $exePath = Join-Path $repoRoot "CPP\7zip\Bundles\Fm\$Platform\7zFM.exe"
    if (-not (Test-Path $exePath)) {
        Write-Error "7zFM.exe not found at: $exePath"
        exit 1
    }
    Write-Host "Launching $exePath"
    Start-Process $exePath
}

exit 0
