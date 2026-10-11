<#
.SYNOPSIS
    Packages 7-Zip (custom build) for distribution: exe installer, portable zip
    and/or Microsoft Store (MSIX) package.

.DESCRIPTION
    Builds the install targets through scripts\build.ps1 -Target All, stages
    7zFM.exe, 7zG.exe, 7z.exe, 7z.dll, 7-zip.dll, License.txt, readme.txt and
    install.ps1 under out\stage, then produces whichever packages were requested in out\:

      -Zip    7-Zip-<ver>-win-<arch>-portable.zip   (Compress-Archive; run install.ps1 inside it
                                                    to install from the extracted folder)
      -Exe    7-Zip-<ver>-win-<arch>-setup.exe      (Inno Setup 6, installer\7-Zip.iss;
                                                    components: GUI, command line, Explorer menu)
      -Store  7-Zip-<ver>-win-<arch>.msix           (makeappx/makepri from the Windows 10/11 SDK,
                                                    installer\AppxManifest.xml)

    Signing is optional. -CertificatePath signs the binaries, the setup exe and
    the MSIX with a .pfx. For the Store, upload the unsigned .msix (the Store
    re-signs it) with -IdentityName/-Publisher/-PublisherDisplayName copied from
    Partner Center. -SelfSign creates a test certificate whose subject matches
    -Publisher so the MSIX can be sideloaded locally.

    For installing the build on this computer without a package, use
    scripts\build.ps1 -Install.

.EXAMPLE
    scripts\create-installer.ps1 -Zip -Exe
.EXAMPLE
    scripts\create-installer.ps1 -All -Version 23.1.1
.EXAMPLE
    scripts\create-installer.ps1 -Store -IdentityName 12345Contoso.7-ZipCustom -Publisher "CN=1A2B3C4D-..." -PublisherDisplayName Contoso
.EXAMPLE
    scripts\create-installer.ps1 -Store -SelfSign     # local sideload test
#>
[CmdletBinding()]
param(
    # Inno Setup exe installer.
    [switch]$Exe,
    # Portable zip of the binaries.
    [switch]$Zip,
    # MSIX package for the Microsoft Store (or sideloading).
    [switch]$Store,
    # All three packages.
    [switch]$All,

    # Package version (major.minor[.patch]); defaults to MY_VERSION_NUMBERS in C\7zVersion.h.
    [string]$Version,
    [ValidateSet('x64', 'arm64')]
    [string]$Platform = 'x64',
    [string]$OutDir,
    # Package the existing build outputs instead of rebuilding.
    [switch]$SkipBuild,

    # Code-signing certificate (.pfx) for the binaries, setup and MSIX.
    [string]$CertificatePath,
    [securestring]$CertificatePassword,
    [string]$TimestampUrl = 'http://timestamp.digicert.com',

    # MSIX identity; must match Partner Center for a Store submission.
    [string]$IdentityName = 'SevenZip.CustomBuild',
    [string]$Publisher = 'CN=7-Zip Custom Build',
    [string]$PublisherDisplayName = '7-Zip (custom build)',
    # Sign the MSIX with a self-signed certificate (subject = -Publisher) for sideloading.
    [switch]$SelfSign
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # Compress-Archive's progress bar is very slow on 5.1
$root = Split-Path -Parent $PSScriptRoot
$installerDir = Join-Path $root 'installer'

if ($All) { $Exe = $true; $Zip = $true; $Store = $true }
if (-not ($Exe -or $Zip -or $Store)) {
    throw 'Choose at least one package: -Exe, -Zip, -Store or -All. See: Get-Help scripts\create-installer.ps1 -Detailed'
}
if ($SelfSign -and $CertificatePath) { throw '-SelfSign and -CertificatePath are mutually exclusive.' }
if (-not $OutDir) { $OutDir = Join-Path $root 'out' }
$OutDir = [IO.Path]::GetFullPath($OutDir)

# staged file -> build output (relative to repo root; {0} = platform)
$StageFiles = [ordered]@{
    '7zFM.exe'    = 'CPP\7zip\Bundles\Fm\{0}\7zFM.exe'
    '7zG.exe'     = 'CPP\7zip\UI\GUI\{0}\7zG.exe'
    '7z.exe'      = 'CPP\7zip\UI\Console\{0}\7z.exe'
    '7z.dll'      = 'CPP\7zip\Bundles\Format7zF\{0}\7z.dll'
    '7-zip.dll'   = 'CPP\7zip\UI\Explorer\{0}\7-zip.dll'
    'License.txt' = 'DOC\License.txt'
    'readme.txt'  = 'DOC\readme.txt'
    'install.ps1' = 'scripts\install.ps1'
}
$SignedFiles = @('7zFM.exe', '7zG.exe', '7z.exe', '7z.dll', '7-zip.dll')

function Invoke-Native([string]$exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $exe) exited with code $LASTEXITCODE" }
}

function Get-DefaultVersion {
    $h = Get-Content (Join-Path $root 'C\7zVersion.h') -Raw
    if ($h -match '#define MY_VERSION_NUMBERS "([0-9.]+)"') { return $Matches[1] }
    throw 'Could not read MY_VERSION_NUMBERS from C\7zVersion.h; pass -Version.'
}

# Newest Windows SDK bin\<ver>\x64 folder that has the given tool.
function Find-SdkTool([string]$name) {
    $bin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $hit = Get-ChildItem $bin -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+(\.\d+){3}$' -and (Test-Path (Join-Path $_.FullName "x64\$name")) } |
        Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if ($hit) { return Join-Path $hit.FullName "x64\$name" }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "$name not found. Install the Windows 10/11 SDK (Visual Studio Installer > Individual components)."
}

function Find-InnoSetup {
    $cmd = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($dir in @("$env:LOCALAPPDATA\Programs\Inno Setup 6", "${env:ProgramFiles(x86)}\Inno Setup 6", "$env:ProgramFiles\Inno Setup 6")) {
        $iscc = Join-Path $dir 'ISCC.exe'
        if (Test-Path $iscc) { return $iscc }
    }
    throw 'Inno Setup 6 (ISCC.exe) not found. Install it with: winget install JRSoftware.InnoSetup'
}

# signtool arguments selecting the certificate: a .pfx file, or a thumbprint in CurrentUser\My.
$script:signArgs = $null
function Invoke-Sign([string[]]$files) {
    if (-not $script:signArgs) { return }
    $timestamp = @()
    if ($TimestampUrl) { $timestamp = @('/tr', $TimestampUrl, '/td', 'SHA256') }
    Invoke-Native (Find-SdkTool 'signtool.exe') (@('sign', '/fd', 'SHA256') + $script:signArgs + $timestamp + $files)
}

# --- Version ------------------------------------------------------------------
if (-not $Version) { $Version = Get-DefaultVersion }
if ($Version -notmatch '^\d+(\.\d+){0,3}$') { throw "Version '$Version' must be numeric, e.g. 23.01" }
$parts = @($Version.Split('.') | ForEach-Object { [int]$_ })
while ($parts.Count -lt 4) { $parts += 0 }
$msixVersion = $parts -join '.'
if ($Store -and $parts[3] -ne 0) { Write-Warning "The Store requires the 4th version field to be 0 ($msixVersion)." }
Write-Host "Packaging 7-Zip (custom build) $Version ($Platform) -> $OutDir"

# --- Certificate ---------------------------------------------------------------
if ($CertificatePath) {
    $CertificatePath = (Resolve-Path $CertificatePath).Path
    $cert = if ($CertificatePassword) {
        New-Object Security.Cryptography.X509Certificates.X509Certificate2($CertificatePath, $CertificatePassword)
    } else {
        New-Object Security.Cryptography.X509Certificates.X509Certificate2($CertificatePath)
    }
    # An MSIX's Publisher must equal the signing certificate's subject.
    if ($Store -and -not $PSBoundParameters.ContainsKey('Publisher')) { $Publisher = $cert.Subject }
    $script:signArgs = @('/f', $CertificatePath)
    if ($CertificatePassword) {
        $plain = [Runtime.InteropServices.Marshal]::PtrToStringUni(
            [Runtime.InteropServices.Marshal]::SecureStringToGlobalAllocUnicode($CertificatePassword))
        $script:signArgs += @('/p', $plain)
    }
}

# --- Build and stage -------------------------------------------------------------
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Target All -Platform $Platform }
$stageRoot = Join-Path $OutDir 'stage'
$appStage = Join-Path $stageRoot 'app'
if (Test-Path $stageRoot) { Remove-Item -Recurse -Force $stageRoot }
New-Item -ItemType Directory -Force $appStage | Out-Null
foreach ($f in $StageFiles.Keys) {
    $src = Join-Path $root ($StageFiles[$f] -f $Platform)
    if (-not (Test-Path $src)) { throw "$src is missing; run without -SkipBuild." }
    Copy-Item $src (Join-Path $appStage $f)
}
Invoke-Sign @($SignedFiles | ForEach-Object { Join-Path $appStage $_ })

$produced = @()
$baseName = "7-Zip-$Version-win-$Platform"

# --- Portable zip ------------------------------------------------------------------
if ($Zip) {
    $zipPath = Join-Path $OutDir "$baseName-portable.zip"
    if (Test-Path $zipPath) { Remove-Item -Force $zipPath }
    Compress-Archive -Path (Join-Path $appStage '*') -DestinationPath $zipPath -CompressionLevel Optimal
    $produced += $zipPath
}

# --- Inno Setup exe installer ---------------------------------------------------------
if ($Exe) {
    $iscc = Find-InnoSetup
    Invoke-Native $iscc @('/Q', "/DAppVersion=$Version", "/DStageDir=$appStage", "/DOutputDir=$OutDir",
        "/DRepoRoot=$root", "/DArch=$Platform", (Join-Path $installerDir '7-Zip.iss'))
    $setupPath = Join-Path $OutDir "$baseName-setup.exe"
    Invoke-Sign @($setupPath)
    $produced += $setupPath
}

# --- MSIX (Microsoft Store) -----------------------------------------------------------
# The 7-Zip icon has only small BMP frames (no 256 px PNG), so logos are scaled
# from the largest frame. Replace them with real artwork for a Store release.
function New-Logo([Drawing.Image]$src, [string]$path, [int]$width, [int]$height, [double]$fill = 1.0) {
    $bmp = New-Object Drawing.Bitmap($width, $height, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::HighQuality
    $side = [int][Math]::Round([Math]::Min($width, $height) * $fill)
    $g.DrawImage($src, [int](($width - $side) / 2), [int](($height - $side) / 2), $side, $side)
    $bmp.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

if ($Store) {
    Add-Type -AssemblyName System.Drawing
    $makeappx = Find-SdkTool 'makeappx.exe'
    $makepri = Find-SdkTool 'makepri.exe'
    $msixStage = Join-Path $stageRoot 'msix'
    $assets = Join-Path $msixStage 'Assets'
    New-Item -ItemType Directory -Force $assets | Out-Null
    Copy-Item (Join-Path $appStage '*') $msixStage -Exclude 'install.ps1'

    $manifest = (Get-Content (Join-Path $installerDir 'AppxManifest.xml') -Raw).
        Replace('@IDENTITY_NAME@', [Security.SecurityElement]::Escape($IdentityName)).
        Replace('@PUBLISHER@', [Security.SecurityElement]::Escape($Publisher)).
        Replace('@PUBLISHER_DISPLAY_NAME@', [Security.SecurityElement]::Escape($PublisherDisplayName)).
        Replace('@VERSION@', $msixVersion).
        Replace('@ARCH@', $Platform)
    [IO.File]::WriteAllText((Join-Path $msixStage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding($false)))

    # Scale- and target-size-qualified logos; resources.pri maps the manifest's plain names to them.
    $icon = New-Object Drawing.Icon((Join-Path $root 'CPP\7zip\UI\FileManager\FM.ico'), 256, 256)
    $png = $icon.ToBitmap()
    foreach ($scale in 100, 200, 400) {
        $k = $scale / 100
        New-Logo $png (Join-Path $assets "StoreLogo.scale-$scale.png") (50 * $k) (50 * $k)
        New-Logo $png (Join-Path $assets "Square44x44Logo.scale-$scale.png") (44 * $k) (44 * $k)
        New-Logo $png (Join-Path $assets "Square150x150Logo.scale-$scale.png") (150 * $k) (150 * $k) 0.6
        New-Logo $png (Join-Path $assets "Wide310x150Logo.scale-$scale.png") (310 * $k) (150 * $k) 0.6
    }
    foreach ($size in 16, 24, 32, 48, 256) {
        New-Logo $png (Join-Path $assets "Square44x44Logo.targetsize-$size.png") $size $size
        New-Logo $png (Join-Path $assets "Square44x44Logo.targetsize-${size}_altform-unplated.png") $size $size
    }
    $png.Dispose(); $icon.Dispose()

    $priConfig = Join-Path $stageRoot 'priconfig.xml'
    Invoke-Native $makepri @('createconfig', '/cf', $priConfig, '/dq', 'en-US', '/pv', '10.0.0', '/o')
    # The default config splits scales into resources.scale-*.pri for bundles; a single .msix needs one .pri.
    [xml]$cfg = Get-Content $priConfig
    $cfg.SelectNodes('//packaging') | ForEach-Object { [void]$_.ParentNode.RemoveChild($_) }
    $cfg.Save($priConfig)
    Invoke-Native $makepri @('new', '/pr', $msixStage, '/cf', $priConfig, '/mn', (Join-Path $msixStage 'AppxManifest.xml'),
        '/of', (Join-Path $msixStage 'resources.pri'), '/o')

    $msixPath = Join-Path $OutDir "$baseName.msix"
    Invoke-Native $makeappx @('pack', '/d', $msixStage, '/p', $msixPath, '/o')

    if ($SelfSign) {
        $friendly = '7-Zip custom build MSIX test signing'
        $selfCert = Get-ChildItem Cert:\CurrentUser\My |
            Where-Object { $_.Subject -eq $Publisher -and $_.FriendlyName -eq $friendly -and $_.NotAfter -gt (Get-Date) } |
            Select-Object -First 1
        if (-not $selfCert) {
            $selfCert = New-SelfSignedCertificate -Type Custom -Subject $Publisher -FriendlyName $friendly `
                -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My `
                -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
        }
        $cerPath = Join-Path $OutDir '7-Zip-test-signing.cer'
        Export-Certificate -Cert $selfCert -FilePath $cerPath | Out-Null
        $script:signArgs = @('/sha1', $selfCert.Thumbprint, '/s', 'My')
        $produced += $cerPath
    }
    Invoke-Sign @($msixPath)
    $produced += $msixPath
}

Write-Host ''
Write-Host 'Packages:'
foreach ($p in $produced) { Write-Host "  $p" }
if ($Store -and $SelfSign) {
    Write-Host ''
    Write-Host 'To sideload the test MSIX, trust the certificate once (elevated PowerShell), then install:'
    Write-Host "  Import-Certificate -FilePath '$cerPath' -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
    Write-Host "  Add-AppxPackage '$msixPath'"
}
