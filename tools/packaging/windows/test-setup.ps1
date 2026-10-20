# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
param([Parameter(Mandatory = $true)][string]$Setup)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or -not $env:RUNNER_TEMP) { throw 'This integration test is for an isolated CI runner.' }
$root = Join-Path $env:RUNNER_TEMP 'GalleonCore setup test'
$env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
$env:QT_PLUGIN_PATH = Join-Path $root 'bin'
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $root 'bin/platforms'
$env:QML_IMPORT_PATH = Join-Path $root 'bin/qml'
$env:QML2_IMPORT_PATH = $env:QML_IMPORT_PATH
$helper = Join-Path $PSScriptRoot 'stop-galleon.ps1' 
$windowsPowerShell = Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$pages = Join-Path $env:RUNNER_TEMP 'galleon-setup-pages.json'
$sentinelRoot = Join-Path $env:LOCALAPPDATA 'galleoncore/wallpaper/personal'
New-Item -ItemType Directory -Force $sentinelRoot | Out-Null
$sentinel = Join-Path $sentinelRoot 'setup-preservation-test.txt'
'Keep this user-owned file.' | Set-Content $sentinel
$settingsKey = 'HKCU:\Software\galleoncore\galleond'
New-Item -Path $settingsKey -Force | Out-Null
New-ItemProperty -Path $settingsKey -Name SetupPreservationTest -Value 'Keep settings' -PropertyType String -Force | Out-Null
$processes = @()

function Run-Installer([string]$Program, [string]$Arguments) {
    $child = Start-Process -FilePath $Program -ArgumentList $Arguments -PassThru
    if (-not $child.WaitForExit(90000)) {
        $child.Kill()
        throw 'Installer exceeded its deadline.'
    }
    if ($child.ExitCode -ne 0) { throw "Installer failed: $($child.ExitCode)" }
}
function Start-Applications {
    $daemon = Start-Process (Join-Path $root 'bin/galleond.exe') -ArgumentList "--pages `"$pages`"" -PassThru
    $bosun = Start-Process (Join-Path $root 'bin/galleon-bosun.exe') -ArgumentList "--minimized --pages `"$pages`"" -PassThru
    $script:processes = @($daemon, $bosun)
    $ready = $false
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        & $windowsPowerShell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $helper -InstallDir $root -Mode Probe
        if ($LASTEXITCODE -eq 0) { $ready = $true; break }
        Start-Sleep -Milliseconds 300
    }
    if (-not $ready) { throw 'Installed Bosun and daemon did not expose their private endpoints.' }
}
function Assert-Closed {
    foreach ($child in $script:processes) {
        if (-not $child.WaitForExit(10000)) { throw 'Setup left an application running.' }
        if ($child.ExitCode -ne 0) { throw 'Application did not shut down cleanly.' }
    }
}
try {
    # First installation, then upgrade while both applications are running.
    Run-Installer (Resolve-Path $Setup).Path "/S /D=$root"
    $startup = [System.Environment]::GetFolderPath('Startup')
    $shell = New-Object -ComObject WScript.Shell
    $daemonStartup = $shell.CreateShortcut((Join-Path $startup 'GalleonCore.lnk'))
    $bosunStartup = $shell.CreateShortcut((Join-Path $startup 'Galleon Bosun.lnk'))
    if ($daemonStartup.TargetPath -ne (Join-Path $root 'bin/galleond.exe')) { throw 'Daemon startup shortcut missing.' }
    if ($bosunStartup.TargetPath -ne (Join-Path $root 'bin/galleon-bosun.exe') -or $bosunStartup.Arguments -ne '--minimized') { throw 'Bosun tray startup shortcut missing.' }
    Start-Applications
    Run-Installer (Resolve-Path $Setup).Path "/S /D=$root"
    Assert-Closed
    if (-not (Test-Path $sentinel)) { throw 'Upgrade removed personal data.' }
    # Uninstall has the same lifecycle guarantee and keeps personal data.
    Start-Applications
    Run-Installer (Join-Path $root 'Uninstall.exe') '/S'
    Assert-Closed
    # NSIS launches a temporary uninstaller copy; its launcher can exit before cleanup ends.
    $markers = @((Join-Path $root 'bin/galleond.exe'), (Join-Path $root 'Uninstall.exe'),
        (Join-Path $startup 'GalleonCore.lnk'), (Join-Path $startup 'Galleon Bosun.lnk'))
    $deadline = [System.DateTime]::UtcNow.AddSeconds(20)
    do {
        $remaining = @($markers | Where-Object { Test-Path -LiteralPath $_ })
        if ($remaining.Count -eq 0) { break }
        Start-Sleep -Milliseconds 100
    } while ([System.DateTime]::UtcNow -lt $deadline)
    if ($remaining.Count -gt 0) { throw "Uninstall left files: $($remaining -join ', ')" }
    if (-not (Test-Path $sentinel)) { throw 'Uninstall removed personal data.' }
    if ((Get-ItemPropertyValue $settingsKey SetupPreservationTest) -ne 'Keep settings') { throw 'Uninstall removed settings.' }
} finally {
    # Only CI-owned processes created by this script may be terminated on failure.
    foreach ($child in $processes) { if (-not $child.HasExited) { $child.Kill() } }
    Remove-Item $sentinel -ErrorAction SilentlyContinue
    Remove-ItemProperty $settingsKey SetupPreservationTest -ErrorAction SilentlyContinue
}
