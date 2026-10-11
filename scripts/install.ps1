<#
.SYNOPSIS
    Installs (or uninstalls) this 7-Zip build on the local system.

.DESCRIPTION
    Components (default: all):
      Gui    7zFM.exe file manager and 7zG.exe; Start menu shortcut "7-Zip File Manager"
      Cli    7z.exe; the install folder is added to PATH
      Shell  7-zip.dll Explorer context menu (uses 7zG.exe and 7z.dll)

    Per-user by default: %LOCALAPPDATA%\Programs\7-Zip and HKCU, no elevation.
    -AllUsers installs to %ProgramFiles%\7-Zip and HKLM (asks for elevation).

    A per-user install overrides the context menu of a system-wide 7-Zip for the
    current user only; uninstalling it brings the system-wide one back. An
    -AllUsers install remembers a previously registered shell extension and
    restores it on uninstall. Folders of other 7-Zip installations (official
    installer) are not overwritten without -Force.

    The script copies itself to the install folder and registers an
    "Apps & features" entry that runs it with -Uninstall.

    Binaries come from the repository build outputs (scripts\build.ps1 -Target All)
    or, with -SourceDir, from a folder holding them (portable zip). When the script
    runs from a folder that contains 7zFM.exe or 7z.exe, that folder is the default source.

.EXAMPLE
    scripts\install.ps1                          # all components, current user
.EXAMPLE
    scripts\install.ps1 -Components Gui,Cli      # without Explorer context menu
.EXAMPLE
    scripts\install.ps1 -AllUsers
.EXAMPLE
    scripts\install.ps1 -Uninstall
#>
[CmdletBinding()]
param(
    [ValidateSet('Gui', 'Cli', 'Shell')]
    [string[]]$Components = @('Gui', 'Cli', 'Shell'),
    [switch]$Uninstall,
    # Program Files + HKLM instead of the current user.
    [switch]$AllUsers,
    [string]$InstallDir,
    [ValidateSet('x64', 'x86', 'arm64', 'arm')]
    [string]$Platform = 'x64',
    # Folder with prebuilt binaries instead of repository build outputs.
    [string]$SourceDir,
    # Install into a folder that has files of another installation.
    [switch]$Force,
    # Restart Explorer so that it releases an old 7-zip.dll and loads the new one.
    [switch]$RestartExplorer
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProductName = '7-Zip (custom build)'
$ArpKeyName = '7-Zip-Custom'          # official 7-Zip uses '7-Zip'
$ManifestName = 'install-manifest.json'
$ShortcutName = '7-Zip File Manager.lnk'
$Clsid = '{23170F69-40C1-278A-1000-000100020000}'
$ShellExtName = '7-Zip Shell Extension'

# install file -> component(s) that need it
$FileComponents = [ordered]@{
    '7zFM.exe'    = @('Gui')
    '7zG.exe'     = @('Gui', 'Shell')
    '7z.exe'      = @('Cli')
    '7z.dll'      = @('Gui', 'Cli', 'Shell')
    '7-zip.dll'   = @('Shell')
    'License.txt' = @('Gui', 'Cli', 'Shell')
    'readme.txt'  = @('Gui', 'Cli', 'Shell')
}
# repository build output of each file (relative to repo root; {0} = platform)
$RepoFiles = @{
    '7zFM.exe'    = 'CPP\7zip\Bundles\Fm\{0}\7zFM.exe'
    '7zG.exe'     = 'CPP\7zip\UI\GUI\{0}\7zG.exe'
    '7z.exe'      = 'CPP\7zip\UI\Console\{0}\7z.exe'
    '7z.dll'      = 'CPP\7zip\Bundles\Format7zF\{0}\7z.dll'
    '7-zip.dll'   = 'CPP\7zip\UI\Explorer\{0}\7-zip.dll'
    'License.txt' = 'DOC\License.txt'
    'readme.txt'  = 'DOC\readme.txt'
}

# --- helpers -------------------------------------------------------------------

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Re-runs this script elevated with the same arguments and exits with its exit code.
function Invoke-Elevated {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    foreach ($kv in $script:BoundArgs.GetEnumerator()) {
        $v = $kv.Value
        if ($v -is [switch] -or $v -is [bool]) {
            if ($v) { $argList += "-$($kv.Key)" }
        } elseif ($v -is [array]) {
            $argList += "-$($kv.Key)"; $argList += ($v -join ',')
        } else {
            $argList += "-$($kv.Key)"; $argList += "`"$v`""
        }
    }
    if ($script:BoundArgs.Keys -notcontains 'AllUsers') { $argList += '-AllUsers' }
    Write-Host 'Requesting elevation for an all-users operation...'
    $p = Start-Process powershell.exe -Verb RunAs -ArgumentList $argList -Wait -PassThru
    exit $p.ExitCode
}

# Registry access goes through .NET, not the PowerShell registry provider:
# the provider treats '*' (as in Software\Classes\*\shellex) as a wildcard.
function Get-Hive {
    $h = if ($script:IsAllUsers) { [Microsoft.Win32.RegistryHive]::LocalMachine } else { [Microsoft.Win32.RegistryHive]::CurrentUser }
    return [Microsoft.Win32.RegistryKey]::OpenBaseKey($h, [Microsoft.Win32.RegistryView]::Registry64)
}

function Set-RegValue([string]$sub, [string]$name, $value, [Microsoft.Win32.RegistryValueKind]$kind = 'String') {
    $k = (Get-Hive).CreateSubKey($sub)
    try { $k.SetValue($name, $value, $kind) } finally { $k.Dispose() }
}

function Get-RegValue([string]$sub, [string]$name) {
    $k = (Get-Hive).OpenSubKey($sub)
    if (-not $k) { return $null }
    try { return $k.GetValue($name) } finally { $k.Dispose() }
}

function Remove-RegKey([string]$sub) { (Get-Hive).DeleteSubKeyTree($sub, $false) }

# Deletes empty keys from (sub) up to (stopAt), e.g. HKCU\Software\Classes\*\shellex
# that were created for our handler keys.
function Remove-EmptyRegKeys([string]$sub, [string]$stopAt) {
    $hive = Get-Hive
    while ($sub -and ($sub.Length -gt $stopAt.Length) -and $sub.StartsWith($stopAt, [StringComparison]::OrdinalIgnoreCase)) {
        $k = $hive.OpenSubKey($sub)
        if ($k) {
            $empty = ($k.SubKeyCount -eq 0 -and $k.ValueCount -eq 0)
            $k.Dispose()
            if (-not $empty) { return }
            $hive.DeleteSubKey($sub, $false)
        }
        $sub = $sub.Substring(0, [Math]::Max(0, $sub.LastIndexOf('\')))
    }
}

function Remove-RegValue([string]$sub, [string]$name) {
    $k = (Get-Hive).OpenSubKey($sub, $true)
    if ($k) { try { $k.DeleteValue($name, $false) } finally { $k.Dispose() } }
}

function Get-ArpKey { "Software\Microsoft\Windows\CurrentVersion\Uninstall\$ArpKeyName" }

function Get-DefaultInstallDir {
    if ($script:IsAllUsers) { return Join-Path $env:ProgramFiles '7-Zip' }
    return Join-Path $env:LOCALAPPDATA 'Programs\7-Zip'
}

function Get-ProgramsFolder {
    if ($script:IsAllUsers) { return [Environment]::GetFolderPath('CommonPrograms') }
    return [Environment]::GetFolderPath('Programs')
}

function Get-Version {
    $h = Join-Path (Split-Path -Parent $PSScriptRoot) 'C\7zVersion.h'
    if (Test-Path $h) {
        $t = Get-Content $h -Raw
        if ($t -match '#define MY_VERSION_NUMBERS "([0-9.]+)"') { return $Matches[1] }
    }
    $fm = Join-Path $script:Source '7zFM.exe'
    if (Test-Path $fm) { return (Get-Item $fm).VersionInfo.ProductVersion }
    return '0'
}

Add-Type -Namespace SevenZipInstall -Name Native -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool MoveFileEx(string existing, string newName, int flags);
'@

function Send-AssocChanged { [SevenZipInstall.Native]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero) }

# A loaded DLL (7-zip.dll in Explorer) or a running exe can't be overwritten or
# deleted, but it can be renamed. The renamed file is deleted later.
function Move-Aside([string]$path) {
    $old = "$path.$([DateTime]::Now.Ticks).old"
    Rename-Item -LiteralPath $path -NewName (Split-Path -Leaf $old) -Force
    # works only elevated; otherwise the next install / uninstall removes it
    [void][SevenZipInstall.Native]::MoveFileEx($old, $null, 4)   # MOVEFILE_DELAY_UNTIL_REBOOT
    $script:MovedAside = $true
    return $old
}

function Copy-InstallFile([string]$src, [string]$dst) {
    if ((Test-Path $dst) -and ((Resolve-Path $src).Path -eq (Resolve-Path $dst).Path)) { return }
    try {
        Copy-Item -LiteralPath $src -Destination $dst -Force
    } catch [System.IO.IOException], [System.UnauthorizedAccessException] {
        if (-not (Test-Path $dst)) { throw }
        Write-Host "  $(Split-Path -Leaf $dst) is in use; replacing it after moving the old file aside"
        Move-Aside $dst | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Force
    }
}

function Remove-InstallFile([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    try {
        Remove-Item -LiteralPath $path -Force
    } catch {
        Write-Host "  $(Split-Path -Leaf $path) is in use; it will be removed later"
        Move-Aside $path | Out-Null
    }
}

function Remove-OldFiles([string]$dir) {
    if (-not (Test-Path $dir)) { return }
    Get-ChildItem -LiteralPath $dir -Filter '*.old' -File -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force -ErrorAction SilentlyContinue }
}

function Read-Manifest([string]$dir) {
    $p = Join-Path $dir $ManifestName
    if (-not (Test-Path $p)) { return $null }
    return Get-Content $p -Raw | ConvertFrom-Json
}

# --- PATH ------------------------------------------------------------------------

function Get-PathScope { if ($script:IsAllUsers) { 'Machine' } else { 'User' } }

function Add-ToPath([string]$dir) {
    $scope = Get-PathScope
    $cur = [Environment]::GetEnvironmentVariable('Path', $scope)
    $parts = @(if ($cur) { $cur.Split(';') | Where-Object { $_ } })
    if ($parts | Where-Object { $_.TrimEnd('\') -ieq $dir.TrimEnd('\') }) { return }
    [Environment]::SetEnvironmentVariable('Path', (($parts + $dir) -join ';'), $scope)
}

function Remove-FromPath([string]$dir) {
    $scope = Get-PathScope
    $cur = [Environment]::GetEnvironmentVariable('Path', $scope)
    if (-not $cur) { return }
    $parts = @($cur.Split(';') | Where-Object { $_ -and ($_.TrimEnd('\') -ine $dir.TrimEnd('\')) })
    $new = $parts -join ';'
    if ($new -ne $cur.TrimEnd(';')) { [Environment]::SetEnvironmentVariable('Path', $new, $scope) }
}

# --- Explorer shell extension ------------------------------------------------------

$ShellExKeys = @(
    '*\shellex\ContextMenuHandlers\7-Zip',
    'Folder\shellex\ContextMenuHandlers\7-Zip',
    'Directory\shellex\ContextMenuHandlers\7-Zip',
    'Directory\shellex\DragDropHandlers\7-Zip',
    'Drive\shellex\DragDropHandlers\7-Zip'
)

$ApprovedKey = 'Software\Microsoft\Windows\CurrentVersion\Shell Extensions\Approved'
$ClassesKey = 'Software\Classes'

# Returns the DLL path of the shell extension that is registered now in our hive.
function Get-RegisteredShellDll { Get-RegValue "$ClassesKey\CLSID\$Clsid\InprocServer32" '' }

function Register-ShellExtension([string]$dll) {
    $clsidKey = "$ClassesKey\CLSID\$Clsid"
    Set-RegValue $clsidKey '' $ShellExtName
    Set-RegValue "$clsidKey\InprocServer32" '' $dll
    Set-RegValue "$clsidKey\InprocServer32" 'ThreadingModel' 'Apartment'
    foreach ($k in $ShellExKeys) { Set-RegValue "$ClassesKey\$k" '' $Clsid }
    Set-RegValue $ApprovedKey $Clsid $ShellExtName
    Send-AssocChanged
}

# previousDll: shell extension that was registered before our install (all-users mode)
function Unregister-ShellExtension([string]$ourDll, [string]$previousDll) {
    $current = Get-RegisteredShellDll
    if ($current -and $ourDll -and ($current -ine $ourDll)) {
        Write-Host "  shell extension is registered to another DLL now ($current); registry is not changed"
        return
    }
    if ($previousDll -and (Test-Path $previousDll)) {
        Write-Host "  restoring previous shell extension: $previousDll"
        Register-ShellExtension $previousDll
        return
    }
    Remove-RegKey "$ClassesKey\CLSID\$Clsid"
    foreach ($k in $ShellExKeys) {
        # only our handler keys; parent keys like Directory\shellex are shared
        $sub = "$ClassesKey\$k"
        if ((Get-RegValue $sub '') -eq $Clsid) {
            Remove-RegKey $sub
            Remove-EmptyRegKeys (Split-Path $sub) $ClassesKey
        }
    }
    Remove-RegValue $ApprovedKey $Clsid
    Remove-EmptyRegKeys $ApprovedKey 'Software\Microsoft\Windows\CurrentVersion'
    Send-AssocChanged
}

# --- install / uninstall ------------------------------------------------------------

function Remove-Integration($manifest, [string]$dir) {
    $comps = @($manifest.Components)
    if ($comps -contains 'Gui') {
        $lnk = Join-Path (Get-ProgramsFolder) $ShortcutName
        if (Test-Path $lnk) { Remove-Item $lnk -Force }
    }
    if ($comps -contains 'Cli') { Remove-FromPath $dir }
    if ($comps -contains 'Shell') {
        $prev = if ($manifest.PSObject.Properties['PreviousShellDll']) { $manifest.PreviousShellDll } else { $null }
        Unregister-ShellExtension (Join-Path $dir '7-zip.dll') $prev
    }
    Remove-RegKey (Get-ArpKey)
}

function Install {
    # source of binaries
    if (-not $SourceDir) {
        if ((Test-Path (Join-Path $PSScriptRoot '7zFM.exe')) -or (Test-Path (Join-Path $PSScriptRoot '7z.exe'))) {
            $SourceDir = $PSScriptRoot
        }
    }
    $script:Source = $SourceDir
    $repoRoot = Split-Path -Parent $PSScriptRoot

    $files = @($FileComponents.Keys | Where-Object {
        $need = $FileComponents[$_]; @($Components | Where-Object { $need -contains $_ }).Count -gt 0 })
    $sources = @{}
    foreach ($f in $files) {
        $src = if ($SourceDir) { Join-Path $SourceDir $f } else { Join-Path $repoRoot ($RepoFiles[$f] -f $Platform) }
        if (-not (Test-Path $src)) {
            if ($f -like '*.txt') { continue }
            throw "$f not found: $src`nBuild it first: scripts\build.ps1 -Target All -Platform $Platform"
        }
        $sources[$f] = $src
    }

    $dir = [IO.Path]::GetFullPath($InstallDir)
    $old = Read-Manifest $dir
    if (-not $old -and (Test-Path $dir) -and @(Get-ChildItem -LiteralPath $dir -Force).Count -gt 0) {
        $hint = if (Test-Path (Join-Path $dir 'Uninstall.exe')) { ' It looks like an official 7-Zip installation.' } else { '' }
        if (-not $Force) {
            throw "$dir is not empty and was not created by this script.$hint Use -InstallDir for another folder, or -Force."
        }
    }
    if ($old -and [bool]$old.AllUsers -ne [bool]$script:IsAllUsers) {
        throw "$dir has an install for $(if ($old.AllUsers) { 'all users' } else { 'the current user' }). Uninstall it first."
    }

    $version = Get-Version
    Write-Host "Installing $ProductName $version ($($Components -join ', ')) to $dir"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Remove-OldFiles $dir

    # previous shell extension (another 7-Zip) to restore on uninstall
    $previousShellDll = $null
    if ($old -and $old.PSObject.Properties['PreviousShellDll']) { $previousShellDll = $old.PreviousShellDll }
    if (-not $old -and $Components -contains 'Shell' -and $script:IsAllUsers) {
        $reg = Get-RegisteredShellDll
        if ($reg -and ($reg -ine (Join-Path $dir '7-zip.dll'))) { $previousShellDll = $reg }
    }

    # reinstall: remove integration of the previous install; files of components that were removed
    if ($old) {
        Remove-Integration $old $dir
        foreach ($f in @($old.Files)) {
            if ($files -notcontains $f) { Remove-InstallFile (Join-Path $dir $f) }
        }
    }

    foreach ($f in $sources.Keys) {
        Write-Host "  $f"
        Copy-InstallFile $sources[$f] (Join-Path $dir $f)
    }
    $selfDst = Join-Path $dir 'install.ps1'
    Copy-InstallFile $PSCommandPath $selfDst

    if ($Components -contains 'Gui') {
        $lnk = Join-Path (Get-ProgramsFolder) $ShortcutName
        $sh = New-Object -ComObject WScript.Shell
        $s = $sh.CreateShortcut($lnk)
        $s.TargetPath = Join-Path $dir '7zFM.exe'
        $s.WorkingDirectory = $dir
        $s.Description = '7-Zip File Manager'
        $s.Save()
        Write-Host "  shortcut: $lnk"
    }
    if ($Components -contains 'Cli') {
        Add-ToPath $dir
        Write-Host "  added to $(Get-PathScope) PATH (new terminals): $dir"
    }
    if ($Components -contains 'Shell') {
        Register-ShellExtension (Join-Path $dir '7-zip.dll')
        Write-Host '  registered Explorer context menu'
    }

    # Apps & features entry
    $arp = Get-ArpKey
    $uninst = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$selfDst`" -Uninstall -InstallDir `"$dir`""
    if ($script:IsAllUsers) { $uninst += ' -AllUsers' }
    $sizeKb = [int]((@($sources.Values | ForEach-Object { (Get-Item $_).Length }) | Measure-Object -Sum).Sum / 1KB)
    $icon = if ($Components -contains 'Gui') { Join-Path $dir '7zFM.exe' } else { Join-Path $dir '7z.exe' }
    $props = @{
        DisplayName = $ProductName; DisplayVersion = $version; Publisher = '7-Zip (custom build)'
        InstallLocation = $dir; DisplayIcon = $icon; UninstallString = $uninst; QuietUninstallString = $uninst
    }
    foreach ($k in $props.Keys) { Set-RegValue $arp $k $props[$k] }
    foreach ($k in 'NoModify', 'NoRepair') { Set-RegValue $arp $k 1 'DWord' }
    Set-RegValue $arp 'EstimatedSize' $sizeKb 'DWord'

    $manifest = [ordered]@{
        Product = $ProductName; Version = $version; Components = @($Components); AllUsers = [bool]$script:IsAllUsers
        InstallDir = $dir; Files = @($sources.Keys | Sort-Object); PreviousShellDll = $previousShellDll
        InstalledAt = (Get-Date).ToString('s')
    }
    $manifest | ConvertTo-Json | Set-Content -Path (Join-Path $dir $ManifestName) -Encoding UTF8

    Write-Host "Installed. Uninstall with: scripts\build.ps1 -Uninstall$(if ($script:IsAllUsers) { ' -AllUsers' }), or from Apps & features."
}

function Uninstall {
    $dir = [IO.Path]::GetFullPath($InstallDir)
    $m = Read-Manifest $dir
    if (-not $m) { throw "No installation made by this script was found in $dir" }
    Write-Host "Uninstalling $ProductName from $dir"
    Remove-Integration $m $dir
    foreach ($f in @($m.Files)) { Remove-InstallFile (Join-Path $dir $f) }
    foreach ($f in 'install.ps1', $ManifestName) { Remove-InstallFile (Join-Path $dir $f) }
    Remove-OldFiles $dir
    if (@(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue).Count -eq 0) {
        Set-Location ([IO.Path]::GetTempPath())
        Remove-Item -LiteralPath $dir -Force -ErrorAction SilentlyContinue
    }
    Write-Host 'Uninstalled.'
}

# --- main ---------------------------------------------------------------------------

$script:BoundArgs = $PSBoundParameters
$script:MovedAside = $false
$script:Source = $null

if (-not $InstallDir -and $Uninstall -and (Test-Path (Join-Path $PSScriptRoot $ManifestName))) {
    # running the installed copy from Apps & features
    $InstallDir = $PSScriptRoot
}
if ($Uninstall -and $InstallDir -and -not $AllUsers) {
    $m = Read-Manifest $InstallDir
    if ($m -and $m.AllUsers) { $AllUsers = $true }
}
$script:IsAllUsers = [bool]$AllUsers
if (-not $InstallDir) { $InstallDir = Get-DefaultInstallDir }

if ($script:IsAllUsers -and -not (Test-Admin)) { Invoke-Elevated }

if ($Uninstall) { Uninstall } else { Install }

if ($RestartExplorer) {
    Write-Host 'Restarting Explorer...'
    Get-Process explorer -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 800
    if (-not (Get-Process explorer -ErrorAction SilentlyContinue)) { Start-Process explorer.exe }
} elseif ($script:MovedAside) {
    Write-Host 'Note: some files were in use (Explorer keeps 7-zip.dll loaded). Old copies are removed on the next install/uninstall,'
    Write-Host '      or restart Explorer (-RestartExplorer) / sign out to load the new shell extension.'
}
exit 0
