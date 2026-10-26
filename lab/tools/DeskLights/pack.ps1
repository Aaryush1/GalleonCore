# Builds, packs and signs the Desk Lights helper as an MSIX (helper + Guard).
# Output goes to lab/out/ (gitignored). Signing uses a self-signed code-signing certificate "CN=Aaryush" in the
# CurrentUser\My store, created on first run with a NON-exportable private key. The private key never leaves that store.
#
# This script does NOT trust the certificate or install the package. Trusting is a machine security setting,
# so Aaryush runs that step himself (printed at the end).
param([string]$Version = "1.0.0.0")
$ErrorActionPreference = "Stop"

$here = $PSScriptRoot
$out = Join-Path $here "..\..\out" | Resolve-Path -ErrorAction SilentlyContinue
if (-not $out) { $out = New-Item -ItemType Directory -Force (Join-Path $here "..\..\out") }
$out = "$out"
$layout = Join-Path $out "layout"
if (Test-Path $layout) { Remove-Item -Recurse -Force $layout }
New-Item -ItemType Directory -Force $layout | Out-Null

# Both executables into one folder; the helper starts Guard.exe from its own directory.
dotnet publish (Join-Path $here "DeskLights.csproj") -c Release -r win-x64 --self-contained false -o $layout | Out-Host
if ($LASTEXITCODE) { throw "publish DeskLights failed" }
dotnet publish (Join-Path $here "..\Guard\Guard.csproj") -c Release -r win-x64 --self-contained false -o $layout | Out-Host
if ($LASTEXITCODE) { throw "publish Guard failed" }

Copy-Item -Recurse (Join-Path $here "Package\Assets") $layout
Copy-Item -Recurse (Join-Path $here "Package\Public") $layout
(Get-Content (Join-Path $here "Package\AppxManifest.xml") -Raw).Replace('$VERSION$', $Version) |
    Set-Content -Encoding utf8 (Join-Path $layout "AppxManifest.xml")

$kit = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.*\x64\makeappx.exe" | Sort-Object FullName | Select-Object -Last 1
if (-not $kit) { throw "makeappx.exe not found (Windows SDK)" }
$makeappx = $kit.FullName
$signtool = Join-Path $kit.DirectoryName "signtool.exe"

$msix = Join-Path $out "DeskLights-$Version.msix"
& $makeappx pack /d $layout /p $msix /o | Out-Host
if ($LASTEXITCODE) { throw "makeappx failed" }

$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object Subject -eq "CN=Aaryush" | Sort-Object NotAfter | Select-Object -Last 1
if (-not $cert) {
    Write-Host "Creating the signing certificate (CN=Aaryush, CurrentUser\My, private key not exportable)"
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject "CN=Aaryush" -FriendlyName "Desk Lights package signing" `
        -KeyExportPolicy NonExportable -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddYears(5)
}
& $signtool sign /fd SHA256 /sha1 $cert.Thumbprint /s My $msix | Out-Host
if ($LASTEXITCODE) { throw "signtool failed" }

$cer = Join-Path $out "DeskLights.cer"
Export-Certificate -Cert $cert -FilePath $cer -Type CERT | Out-Null

Write-Host ""
Write-Host "Built and signed: $msix"
Write-Host "Public certificate: $cer (thumbprint $($cert.Thumbprint))"
Write-Host ""
Write-Host "One-time trust step (Aaryush runs this, in an admin PowerShell):"
Write-Host "  Import-Certificate -FilePath `"$cer`" -CertStoreLocation Cert:\LocalMachine\TrustedPeople"
Write-Host "Then install or update:"
Write-Host "  Add-AppxPackage -Path `"$msix`" -ForceApplicationShutdown"
