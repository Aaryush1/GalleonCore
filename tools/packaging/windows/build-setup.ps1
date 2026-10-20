# SPDX-FileCopyrightText: 2026 Paul Woisard
# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory = $true)][string]$StagingDir,
    [Parameter(Mandatory = $true)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$OutputDir = '.'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path $StagingDir).Path.TrimEnd([char[]]'\/')
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$output = (Resolve-Path $OutputDir).Path
$nsis = Join-Path ${env:ProgramFiles(x86)} 'NSIS/makensis.exe'
if (-not (Test-Path $nsis)) { throw 'NSIS is required to build Setup.' }
$entries = @(Get-ChildItem $root -Recurse -Force)
$lines = @('; Generated from the deployed payload; remove only installed files.')
foreach ($entry in $entries) {
    if ($entry.Attributes -band [System.IO.FileAttributes]::ReparsePoint) { throw 'Reparse points are not allowed in Setup payloads.' }
    $relative = $entry.FullName.Substring($root.Length + 1)
    if ($relative -match '[\$"\r\n]') { throw "Unsafe NSIS path: $relative" }
}
foreach ($entry in ($entries | Where-Object { -not $_.PSIsContainer } | Sort-Object FullName)) {
    $relative = $entry.FullName.Substring($root.Length + 1)
    $lines += 'Delete "$INSTDIR\' + $relative + '"'
}
foreach ($entry in ($entries | Where-Object { $_.PSIsContainer } | Sort-Object { $_.FullName.Length } -Descending)) {
    $relative = $entry.FullName.Substring($root.Length + 1)
    $lines += 'RMDir "$INSTDIR\' + $relative + '"'
}
$payloadManifest = Join-Path $output 'install-payload.txt'
$entries | Where-Object { -not $_.PSIsContainer } | ForEach-Object {
    $_.FullName.Substring($root.Length + 1)
} | Set-Content $payloadManifest -Encoding utf8
$manifest = Join-Path $output 'uninstall-payload.nsh' 
$lines | Set-Content $manifest -Encoding utf8
$setup = Join-Path $output "GalleonCore-$Version-Windows-x86_64-Setup.exe"
& $nsis '/WX' "/DVERSION=$Version" "/DSTAGING_DIR=$root" "/DOUTPUT_FILE=$setup" "/DUNINSTALL_MANIFEST=$manifest" "/DPAYLOAD_MANIFEST=$payloadManifest" "$PSScriptRoot/galleoncore.nsi"
if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed.' }
(Get-FileHash $setup -Algorithm SHA256).Hash.ToLower() + '  ' + [System.IO.Path]::GetFileName($setup) |
    Set-Content (Join-Path $output 'Windows-Setup-SHA256SUMS') -Encoding ascii
