[CmdletBinding()]
param(
    [string]$QMake,
    [string]$MakeCommand = 'nmake',
    [string]$OpenSslDir,
    [switch]$Clean,
    [switch]$NoBuild,
    [switch]$NoLaunch,
    [switch]$StopExisting,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$AppArgs
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
$BuildDir = Join-Path $PSScriptRoot 'dev-build'

$running = Get-Process LANShare -ErrorAction SilentlyContinue
foreach ($process in $running) {
    if (-not $process.Path) {
        continue
    }

    if (-not $StopExisting) {
        throw "LANShare is already running from '$($process.Path)'. Close it first, or rerun with -StopExisting if no transfers are active."
    }

    Stop-Process -Id $process.Id -Force
    Wait-Process -Id $process.Id -Timeout 10 -ErrorAction SilentlyContinue
}

if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$QMake = Resolve-Tool -PreferredName $QMake -FallbackNames @('qmake', 'qmake-qt6', 'qmake-qt5')
if (-not $QMake) {
    throw 'Could not find qmake. Pass -QMake C:\path\to\qmake.exe.'
}

$MakeCommand = Resolve-Tool -PreferredName $MakeCommand -FallbackNames @('nmake', 'mingw32-make', 'make')
if (-not $MakeCommand) {
    throw 'Could not find a make tool. Start a Visual Studio Developer shell or pass -MakeCommand.'
}

if (-not $NoBuild) {
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
}

$Exe = Get-ChildItem -Path $BuildDir -Filter 'LANShare.exe' -Recurse |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $Exe) {
    throw 'Could not find LANShare.exe. Run without -NoBuild first.'
}

Copy-OpenSslRuntime -DestinationDir $Exe.DirectoryName -PreferredDir $OpenSslDir

$qtBin = Split-Path -Parent $QMake
$env:PATH = "$qtBin;$env:PATH"

if ($NoLaunch) {
    Write-Host "Build ready: $($Exe.FullName)"
    return
}

Write-Host "Launching $($Exe.FullName)"
Start-Process -FilePath $Exe.FullName -ArgumentList $AppArgs -WorkingDirectory $RepoRoot
