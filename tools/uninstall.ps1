# KarmaTV PostFX - rimozione da rFactor 2.
# Toglie solo cio' che install.ps1 ha messo (elenco in KarmaTV-PostFX\install-manifest.txt).
# I preset KarmaTV restano, a meno di -RemovePresets.
#
# Uso:
#   .\tools\uninstall.ps1 -Rf2Bin64 "E:\SteamLibrary\steamapps\common\rFactor 2\Bin64"
#   .\tools\uninstall.ps1 -Rf2Bin64 "..." -RemovePresets

param(
    [Parameter(Mandatory = $true)][string]$Rf2Bin64,
    [switch]$RemovePresets
)

$ErrorActionPreference = 'Stop'

if (Get-Process -Name 'rFactor2' -ErrorAction SilentlyContinue) {
    throw "rFactor 2 e' in esecuzione: chiudilo prima di disinstallare."
}

$ktvDir = Join-Path $Rf2Bin64 'KarmaTV-PostFX'
$manifestPath = Join-Path $ktvDir 'install-manifest.txt'
if (-not (Test-Path $manifestPath)) {
    throw "Manifest non trovato ($manifestPath): niente da rimuovere o installazione fatta a mano."
}

foreach ($entry in Get-Content $manifestPath) {
    $kind, $relative = $entry -split ':', 2
    $path = Join-Path $Rf2Bin64 $relative
    switch ($kind) {
        'file' {
            if (-not (Test-Path $path)) { continue }
            # dxgi.dll si toglie solo se e' davvero ReShade.
            if ($relative -eq 'dxgi.dll' -and (Get-Item $path).VersionInfo.ProductName -notmatch 'ReShade') {
                Write-Warning "dxgi.dll non e' ReShade: lasciato al suo posto."
                continue
            }
            Remove-Item $path -Force
            Write-Host "Rimosso $relative"
        }
        'dir' {
            if (Test-Path $path) {
                Remove-Item $path -Recurse -Force
                Write-Host "Rimossa la cartella $relative"
            }
        }
        'backup' {
            # Ripristina il dxgi.dll che c'era prima di ReShade.
            if (Test-Path $path) {
                Move-Item $path (Join-Path $Rf2Bin64 'dxgi.dll') -Force
                Write-Host "Ripristinato il dxgi.dll originale"
            }
        }
    }
}

if ($RemovePresets) {
    Remove-Item $ktvDir -Recurse -Force
    Write-Host "Rimossa la cartella KarmaTV-PostFX con i preset."
}
else {
    Remove-Item $manifestPath -Force
    Write-Host "Preset KarmaTV conservati in $ktvDir"
}
Write-Host "Disinstallazione completata."
