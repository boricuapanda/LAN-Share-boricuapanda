[CmdletBinding()]
param(
    [string]$QMake,
    [string]$MakeCommand,
    [string]$Windeployqt,
    [string]$InnoSetupCompiler,
    [string]$OpenSslDir,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

function Resolve-Tool {
    param(
        [string]$PreferredName,
        [string[]]$FallbackNames = @()
    )

    if ($PreferredName) {
        return $PreferredName
    }

    foreach ($name in $FallbackNames) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if ($cmd) {
            return $cmd.Source
        }
    }

    return $null
}

function Get-AppVersion {
    param([string]$Path)

    $content = Get-Content $Path -Raw
    $major = [regex]::Match($content, 'PROGRAM_X_VER\{(\d+)\}').Groups[1].Value
    $minor = [regex]::Match($content, 'PROGRAM_Y_VER\{(\d+)\}').Groups[1].Value
    $patch = [regex]::Match($content, 'PROGRAM_Z_VER\{(\d+)\}').Groups[1].Value

    if (-not $major -or -not $minor -or -not $patch) {
        throw "Could not parse app version from $Path"
    }

    return "$major.$minor.$patch"
}

function Copy-OpenSslRuntime {
    param(
        [string]$DestinationDir,
        [string]$PreferredDir
    )

    $exactCandidateDirs = @()
    if ($PreferredDir) {
        $exactCandidateDirs += $PreferredDir
    }
    $exactCandidateDirs += @(
        'C:\tmp\OpenSSL\bin',
        'C:\tmp\OpenSSL-Win64\bin',
        (Join-Path $PSScriptRoot '..\..\.tools\OpenSSL-Win64\bin'),
        'C:\OpenSSL-Win64\bin',
        'C:\Program Files\OpenSSL-Win64\bin',
        'C:\Program Files (x86)\OpenSSL-Win64\bin'
    )

    foreach ($dir in $exactCandidateDirs) {
        if (-not $dir -or -not (Test-Path $dir)) {
            continue
        }

        $crypto = Join-Path $dir 'libcrypto-1_1-x64.dll'
        $ssl = Join-Path $dir 'libssl-1_1-x64.dll'
        if ((Test-Path $crypto) -and (Test-Path $ssl)) {
            Copy-Item -Force $crypto (Join-Path $DestinationDir 'libcrypto-1_1-x64.dll')
            Copy-Item -Force $ssl (Join-Path $DestinationDir 'libssl-1_1-x64.dll')
            Write-Host "Bundled OpenSSL 1.1 x64 runtime from $dir"
            return
        }
    }

    $fallbackCandidateDirs = @(
        'C:\Users\RC\AppData\Local\Programs\Python\Python310\DLLs',
        'C:\Users\RC\AppData\Local\Programs\Python\Python311\DLLs'
    )

    foreach ($dir in $fallbackCandidateDirs) {
        if (-not $dir -or -not (Test-Path $dir)) {
            continue
        }

        $crypto = Join-Path $dir 'libcrypto-1_1.dll'
        $ssl = Join-Path $dir 'libssl-1_1.dll'
        if ((Test-Path $crypto) -and (Test-Path $ssl)) {
            Copy-Item -Force $crypto (Join-Path $DestinationDir 'libcrypto-1_1.dll')
            Copy-Item -Force $ssl (Join-Path $DestinationDir 'libssl-1_1.dll')
            Copy-Item -Force $crypto (Join-Path $DestinationDir 'libcrypto-1_1-x64.dll')
            Copy-Item -Force $ssl (Join-Path $DestinationDir 'libssl-1_1-x64.dll')
            Write-Warning "Bundled Python OpenSSL 1.1 fallback from $dir. Install OpenSSL-Win64 and pass -OpenSslDir for release packaging."
            return
        }
    }

    Write-Warning 'OpenSSL 1.1 runtime DLLs were not found. TLS transfers will fail unless libssl-1_1-x64.dll and libcrypto-1_1-x64.dll are beside LANShare.exe or on PATH.'
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$SrcDir = Join-Path $RepoRoot 'src'
$BuildDir = Join-Path $PSScriptRoot 'build'
$StageDir = Join-Path $PSScriptRoot 'bin'
$DistDir = Join-Path $RepoRoot 'dist\windows'
$InstallerScript = Join-Path $PSScriptRoot 'LANShare_setup_script.iss'
$VersionFile = Join-Path $SrcDir 'settings.h'

if ($Clean) {
    foreach ($path in @($BuildDir, $StageDir, $DistDir)) {
        if (Test-Path $path) {
            Remove-Item -Recurse -Force $path
        }
    }
}

New-Item -ItemType Directory -Force -Path $BuildDir, $StageDir, $DistDir | Out-Null

$QMake = Resolve-Tool -PreferredName $QMake -FallbackNames @('qmake', 'qmake-qt6', 'qmake-qt5')
if (-not $QMake) {
    throw 'Could not find qmake. Install Qt or pass -QMake C:\path\to\qmake.exe.'
}

$MakeCommand = Resolve-Tool -PreferredName $MakeCommand -FallbackNames @('mingw32-make', 'nmake', 'make')
if (-not $MakeCommand) {
    throw 'Could not find a make tool. Install MinGW/MSVC build tools or pass -MakeCommand.'
}

$Windeployqt = Resolve-Tool -PreferredName $Windeployqt -FallbackNames @('windeployqt')
if (-not $Windeployqt) {
    throw 'Could not find windeployqt. Install Qt deployment tools or pass -Windeployqt.'
}

$InnoSetupCompiler = Resolve-Tool -PreferredName $InnoSetupCompiler -FallbackNames @('iscc', 'ISCC')
if (-not $InnoSetupCompiler) {
    throw 'Could not find the Inno Setup compiler (ISCC.exe) or pass -InnoSetupCompiler.'
}

$AppVersion = Get-AppVersion $VersionFile

Push-Location $BuildDir
try {
    & $QMake (Join-Path $SrcDir 'LANShare.pro') 'CONFIG+=release'
    if ($LASTEXITCODE -ne 0) { throw "qmake failed with exit code $LASTEXITCODE" }

    $makeName = [System.IO.Path]::GetFileNameWithoutExtension($MakeCommand).ToLowerInvariant()
    if ($makeName -eq 'mingw32-make' -or $makeName -eq 'make') {
        & $MakeCommand "-j$([Environment]::ProcessorCount)"
    } else {
        & $MakeCommand
    }
    if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }
}
finally {
    Pop-Location
}

$Exe = Get-ChildItem -Path $BuildDir -Filter 'LANShare.exe' -Recurse |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $Exe) {
    throw 'Could not find LANShare.exe after build.'
}

Copy-Item -Force $Exe.FullName $StageDir

& $Windeployqt --release --compiler-runtime --dir $StageDir $Exe.FullName
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

Copy-OpenSslRuntime -DestinationDir $StageDir -PreferredDir $OpenSslDir

& $InnoSetupCompiler "/DMyAppVersion=$AppVersion" "/O$DistDir" $InstallerScript
if ($LASTEXITCODE -ne 0) { throw "Inno Setup packaging failed with exit code $LASTEXITCODE" }

Write-Host "Installer created in $DistDir"
