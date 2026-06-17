[CmdletBinding()]
param(
    [string]$QMake = 'C:\tmp\Qt\5.15.2\msvc2019_64\bin\qmake.exe',
    [string]$MakeCommand = 'nmake',
    [string]$Windeployqt = 'C:\tmp\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe',
    [string]$InnoSetupCompiler = 'C:\tmp\InnoSetup\ISCC.exe',
    [string]$OpenSslDir,
    [switch]$Clean
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
$Target = Join-Path $ScriptDir 'packaging\windows\build-installer.ps1'

& $Target `
    -QMake $QMake `
    -MakeCommand $MakeCommand `
    -Windeployqt $Windeployqt `
    -InnoSetupCompiler $InnoSetupCompiler `
    -OpenSslDir $OpenSslDir `
    -Clean:$Clean
exit $LASTEXITCODE
