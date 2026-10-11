<#
.SYNOPSIS
    Build 7-zip targets using NMAKE with Visual Studio toolchain.
    Optionally installs the build to the local system (or uninstalls it).
.DESCRIPTION
    -Install builds the targets required by -Components and installs them with
    scripts\install.ps1 (per-user by default, -AllUsers for Program Files).
    Components: Gui   - 7zFM.exe file manager, 7zG.exe, Start menu shortcut
                Cli   - 7z.exe, added to PATH
                Shell - 7-zip.dll Explorer context menu (uses 7zG.exe)
    Default is all components.
.EXAMPLE
    .\build.ps1 -Target Fm
    .\build.ps1 -Target Alone2 -Platform x64
    .\build.ps1 -Target FileManager -Clean
    .\build.ps1 -Target Fm -Launch
    .\build.ps1 -Target All                      # every target that the installers package
    .\build.ps1 -Install                         # build and install Gui, Cli and Shell for current user
    .\build.ps1 -Install -Components Gui,Cli     # no Explorer context menu
    .\build.ps1 -Install -AllUsers               # Program Files, all users (asks for elevation)
    .\build.ps1 -Uninstall
#>
param(
    [Parameter(Mandatory = $false)]
    [ValidateSet(
        'All',            # targets required by install / installers (Fm, Format7zF, Console, GUI, Explorer)
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

    [switch]$Launch,

    # Build the required targets and install them to the local system.
    [switch]$Install,
    # Remove an installation that was made with -Install.
    [switch]$Uninstall,
    # Components for -Install. Default: all.
    [ValidateSet('Gui', 'Cli', 'Shell')]
    [string[]]$Components = @('Gui', 'Cli', 'Shell'),
    # -Install / -Uninstall for all users (Program Files, HKLM). Requires elevation.
    [switch]$AllUsers,
    # Install folder. Default: %LOCALAPPDATA%\Programs\7-Zip or %ProgramFiles%\7-Zip with -AllUsers.
    [string]$InstallDir,
    # -Install: install existing binaries without building.
    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path $PSScriptRoot -Parent

if ($Install -and $Uninstall) { throw '-Install and -Uninstall are mutually exclusive.' }

function Get-InstallArgs {
    $a = @{ Platform = $Platform }
    if ($AllUsers) { $a.AllUsers = $true }
    if ($InstallDir) { $a.InstallDir = $InstallDir }
    return $a
}

if ($Uninstall) {
    $a = Get-InstallArgs
    & (Join-Path $PSScriptRoot 'install.ps1') -Uninstall @a
    exit $LASTEXITCODE
}

# Targets that produce the files of each install component.
$componentTargets = @{
    Gui   = @('Fm', 'GUI', 'Format7zF')
    Cli   = @('Console', 'Format7zF')
    Shell = @('Explorer', 'GUI', 'Format7zF')
}
$allTargets = @('Fm', 'Format7zF', 'Console', 'GUI', 'Explorer')

if ($Install) {
    $targets = @($Components | ForEach-Object { $componentTargets[$_] } | Select-Object -Unique)
    if ($Clean) { Write-Warning '-Clean is ignored with -Install.'; $Clean = $false }
} elseif ($Target -eq 'All') {
    $targets = $allTargets
} else {
    $targets = @($Target)
}

$bundleTargets = @('Alone', 'Alone2', 'Alone7z', 'Fm', 'Format7z', 'Format7zF',
                   'Format7zR', 'Format7zExtract', 'Format7zExtractR', 'LzmaCon',
                   'SFXCon', 'SFXSetup', 'SFXWin')

# Find Visual Studio via vswhere
function Find-VcVarsAll {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        $vswhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    }
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe not found. Install Visual Studio Build Tools.'
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) {
        throw 'No Visual Studio installation with VC++ tools found.'
    }
    $vcvarsall = Join-Path $vsPath 'VC\Auxiliary\Build\vcvarsall.bat'
    if (-not (Test-Path $vcvarsall)) {
        throw "vcvarsall.bat not found at: $vcvarsall"
    }
    return $vcvarsall
}

# Map -Platform to vcvarsall architecture argument
$vcArch = switch ($Platform) {
    'x64'   { 'x64' }
    'x86'   { 'x86' }
    'arm64' { 'x64_arm64' }
    'arm'   { 'x64_arm' }
}

function Invoke-Build([string]$t) {
    if ($bundleTargets -contains $t) {
        $makeDir = Join-Path $repoRoot "CPP\7zip\Bundles\$t"
    } else {
        $makeDir = Join-Path $repoRoot "CPP\7zip\UI\$t"
    }
    if (-not (Test-Path $makeDir)) {
        throw "Target directory not found: $makeDir"
    }

    $nmakeTarget = if ($Clean) { 'clean' } else { '' }
    $nmakeArgs   = "PLATFORM=$Platform"

    Write-Host "Target:   $t ($makeDir)"
    Write-Host "Platform: $Platform (vcvarsall arch: $vcArch)"
    if ($Clean) { Write-Host "Action:   clean" }
    Write-Host ""

    # Run vcvarsall + nmake inside cmd so the VS env is active for the build.
    # vcvarsall output goes to nul: it prints harmless messages to stderr, which
    # PowerShell turns into errors when the output of this script is redirected.
    $cmd = "call `"$script:vcvarsall`" $vcArch >nul 2>&1 && cd /d `"$makeDir`" && nmake -nologo $nmakeArgs $nmakeTarget 2>&1"
    cmd /c $cmd
    if ($LASTEXITCODE -ne 0) { throw "Build of $t failed (nmake exit code $LASTEXITCODE)." }
}

if (-not ($Install -and $SkipBuild)) {
    $script:vcvarsall = Find-VcVarsAll
    foreach ($t in $targets) { Invoke-Build $t }
}

if ($Install) {
    $a = Get-InstallArgs
    & (Join-Path $PSScriptRoot 'install.ps1') -Components $Components @a
    exit $LASTEXITCODE
}

if ($Launch) {
    $exePath = Join-Path $repoRoot "CPP\7zip\Bundles\Fm\$Platform\7zFM.exe"
    if (-not (Test-Path $exePath)) {
        throw "7zFM.exe not found at: $exePath"
    }
    Write-Host "Launching $exePath"
    Start-Process $exePath
}

exit 0
