[CmdletBinding()]
param(
    [string]$QMake = 'C:\tmp\Qt\5.15.2\msvc2019_64\bin\qmake.exe',
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

function Import-VcVars {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat'
    )

    $vcvars = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $vcvars) {
        return
    }

    cmd.exe /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^(.*?)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
        }
    }
}

Import-VcVars

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Target = Join-Path $ScriptDir 'packaging\windows\run-app.ps1'

$innerArgs = @{
    QMake = $QMake
    MakeCommand = $MakeCommand
}
if ($OpenSslDir) { $innerArgs.OpenSslDir = $OpenSslDir }
if ($Clean) { $innerArgs.Clean = $true }
if ($NoBuild) { $innerArgs.NoBuild = $true }
if ($NoLaunch) { $innerArgs.NoLaunch = $true }
if ($StopExisting) { $innerArgs.StopExisting = $true }

& $Target @innerArgs @AppArgs
exit $LASTEXITCODE
