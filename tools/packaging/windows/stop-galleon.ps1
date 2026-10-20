# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$InstallDir,
    [ValidateSet('Stop', 'Probe', 'Prepare')][string]$Mode = 'Stop'
)
$ErrorActionPreference = 'Stop'

function Get-ControlName {
    $sid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($sid))
        $digest = ([System.BitConverter]::ToString($bytes)).Replace('-', '').ToLowerInvariant()
        return "galleoncore-$($digest.Substring(0, 24))"
    } finally { $sha.Dispose() }
}

function Send-Control([string]$Name, [string]$Action) {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new('.', $Name, [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect(1500)
        $frame = [System.Text.Encoding]::UTF8.GetBytes("{`"action`":`"$Action`"}`n")
        $pipe.Write($frame, 0, $frame.Length)
        $pipe.Flush()
        return $true
    } catch {
        return $false
    } finally { $pipe.Dispose() }
}

function Remove-PreviousPayload([string]$Root) {
    $manifest = [System.IO.Path]::Combine($Root, 'install-payload.txt')
    if (-not (Test-Path -LiteralPath $manifest)) {
        if ((Test-Path -LiteralPath $Root) -and (Get-ChildItem -LiteralPath $Root -Force | Select-Object -First 1)) {
            throw 'Choose an empty installation folder or an existing GalleonCore Setup installation.'
        }
        return
    }
    if ((Get-Item -LiteralPath $manifest).Length -gt 4194304) { throw 'Installed payload manifest is too large.' }
    $prefix = $Root.TrimEnd([char[]]'\/') + [System.IO.Path]::DirectorySeparatorChar
    $files = @()
    foreach ($relative in @(Get-Content -LiteralPath $manifest -Encoding UTF8)) {
        if ($relative.Length -gt 4096 -or [System.IO.Path]::IsPathRooted($relative) -or $relative.Contains(':')) {
            throw 'Invalid installed payload path.'
        }
        $path = [System.IO.Path]::GetFullPath([System.IO.Path]::Combine($Root, $relative))
        if (-not $path.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Payload path escapes installation.' }
        $inspect = $path
        while ($inspect -and $inspect.StartsWith($Root, [System.StringComparison]::OrdinalIgnoreCase)) {
            if ((Test-Path -LiteralPath $inspect) -and ((Get-Item -LiteralPath $inspect -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
                throw 'Refusing to remove payload through a reparse point.'
            }
            if ([string]::Equals($inspect, $Root, [System.StringComparison]::OrdinalIgnoreCase)) { break }
            $inspect = [System.IO.Path]::GetDirectoryName($inspect)
        }
        $files += $path
        if ($files.Count -gt 10000) { throw 'Too many installed payload paths.' }
    }
    # Validate the entire manifest before deleting a single file. Unknown files,
    # including user additions, are preserved; no recursive directory deletion.
    foreach ($path in $files) { if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force } }
}

$root = [System.IO.Path]::GetFullPath($InstallDir)
$expected = @('galleond.exe', 'galleon-bosun.exe') | ForEach-Object {
    [System.IO.Path]::Combine($root, 'bin', $_)
}
$targets = @(Get-Process -Name galleond, galleon-bosun -ErrorAction SilentlyContinue | Where-Object {
    try {
        $path = $_.Path
        if ($_.SessionId -eq [System.Diagnostics.Process]::GetCurrentProcess().SessionId) { $true }
        else { $expected | Where-Object { [string]::Equals($_, $path, [System.StringComparison]::OrdinalIgnoreCase) } }
    } catch { $false }
})
$control = Get-ControlName
if ($Mode -eq 'Probe') {
    if ($targets.Count -ne 2) { exit 1 }
    $bosunReady = Send-Control "$control-bosun" 'ping'
    $daemonReady = Send-Control $control 'ping'
    if ($bosunReady -and $daemonReady) { exit 0 }
    exit 1
}

# The per-user named pipes identify GalleonCore even if a ZIP preview was used
# before installation. A missing endpoint is normal on the first installation.
[void](Send-Control "$control-bosun" 'stop')
[void](Send-Control $control 'stop')
$deadline = [System.DateTime]::UtcNow.AddSeconds(20)
do {
    $remaining = @($targets | Where-Object { -not $_.HasExited })
    if ($remaining.Count -eq 0) {
        if ($Mode -eq 'Prepare') { Remove-PreviousPayload $root }
        exit 0
    }
    Start-Sleep -Milliseconds 100
} while ([System.DateTime]::UtcNow -lt $deadline)

Write-Error 'GalleonCore could not stop cleanly. No application files should be replaced. Close Bosun and galleond, then retry.'
exit 1
