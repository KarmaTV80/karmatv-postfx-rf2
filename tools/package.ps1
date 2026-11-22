# KarmaTV PostFX - crea l'installer da distribuire (dist\KarmaTV-PostFX-Setup-<versione>.exe).
#
# 1. compila la versione Release;
# 2. prepara dist\payload: add-on, shader, atlante delle LUT, ReShade64.dll (verificato con
#    SHA-256), licenze, install.ps1 e uninstall.ps1;
# 3. compila tools\installer\KarmaTV-PostFX.iss con Inno Setup 6.
#
# Uso:
#   .\tools\package.ps1                 (versione presa da CMakeLists.txt)
#   .\tools\package.ps1 -Version 1.0.0

param(
    [string]$Version,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# --- Versione ------------------------------------------------------------------
if (-not $Version) {
    $match = Select-String -Path (Join-Path $root 'CMakeLists.txt') -Pattern 'project\(KarmaTV_PostFX VERSION ([0-9.]+)'
    if (-not $match) { throw "Versione non trovata in CMakeLists.txt: usa -Version." }
    $Version = $match.Matches[0].Groups[1].Value
}
Write-Host "KarmaTV PostFX $Version"

# --- Strumenti -----------------------------------------------------------------
$iscc = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe"
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6 non trovato. Installalo con: winget install JRSoftware.InnoSetup" }

# --- Compilazione --------------------------------------------------------------
if (-not $SkipBuild) {
    $cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
    if (-not $cmake) {
        $cmake = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
    Push-Location $root
    try {
        & $cmake --build --preset release
        if ($LASTEXITCODE -ne 0) { throw "Compilazione non riuscita." }
    }
    finally { Pop-Location }
}

$release = Join-Path $root 'build\x64\Release'
foreach ($file in 'KarmaTV_PostFX.addon64', 'ktv_lut.exe') {
    if (-not (Test-Path (Join-Path $release $file))) { throw "$file non trovato in $release." }
}

# --- Pacchetto -----------------------------------------------------------------
$dist = Join-Path $root 'dist'
$payload = Join-Path $dist 'payload'
if (Test-Path $payload) { Remove-Item $payload -Recurse -Force }
New-Item -ItemType Directory -Force -Path $payload, "$payload\shaders", "$payload\textures", "$payload\licenses", "$payload\fonts" | Out-Null

Copy-Item (Join-Path $release 'KarmaTV_PostFX.addon64') $payload
Copy-Item (Join-Path $root 'shaders\*') "$payload\shaders"
Copy-Item (Join-Path $root 'tools\install.ps1'), (Join-Path $root 'tools\uninstall.ps1') $payload
Copy-Item (Join-Path $root 'third_party\reshade\LICENSE.md') "$payload\licenses\ReShade-LICENSE.md"
Copy-Item (Join-Path $root 'third_party\fonts\*') "$payload\fonts"

& (Join-Path $release 'ktv_lut.exe') atlas "$payload\textures\KarmaTV_LUTs.png" (Join-Path $root 'luts') | Out-Null
if (-not (Test-Path "$payload\textures\KarmaTV_LUTs.png")) { throw "Atlante delle LUT non creato." }

# ReShade64.dll: estratto dall'installer ufficiale (versione e hash fissati in install.ps1)
$installScript = Get-Content (Join-Path $root 'tools\install.ps1') -Raw
$reshadeVersion = [regex]::Match($installScript, "\`$ReShadeVersion\s*=\s*'([^']+)'").Groups[1].Value
$setupHash = [regex]::Match($installScript, "\`$ReShadeSetupHash\s*=\s*'([^']+)'").Groups[1].Value
$dllHash = [regex]::Match($installScript, "\`$ReShadeDllHash\s*=\s*'([^']+)'").Groups[1].Value

$cache = Join-Path $env:LOCALAPPDATA 'KarmaTV-PostFX\cache'
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$setup = Join-Path $cache "ReShade_Setup_${reshadeVersion}_Addon.exe"
if (-not (Test-Path $setup) -or (Get-FileHash $setup -Algorithm SHA256).Hash -ne $setupHash) {
    Write-Host "Scarico ReShade $reshadeVersion da reshade.me ..."
    Invoke-WebRequest -Uri "https://reshade.me/downloads/ReShade_Setup_${reshadeVersion}_Addon.exe" -OutFile $setup -UseBasicParsing
}
if ((Get-FileHash $setup -Algorithm SHA256).Hash -ne $setupHash) { throw "Installer di ReShade con hash inatteso." }

# Lettura diretta del file ZIP accodato all'installer di ReShade (non viene eseguito)
$bytes = [System.IO.File]::ReadAllBytes($setup)
$dll = $null
for ($i = 0; $i -lt $bytes.Length - 30 -and $dll -eq $null; $i++) {
    if ($bytes[$i] -ne 0x50 -or $bytes[$i + 1] -ne 0x4B -or $bytes[$i + 2] -ne 3 -or $bytes[$i + 3] -ne 4) { continue }
    $nameLength = [BitConverter]::ToUInt16($bytes, $i + 26)
    $name = [System.Text.Encoding]::ASCII.GetString($bytes, $i + 30, $nameLength)
    if ($name -ne 'ReShade64.dll') { continue }
    $compressed = [BitConverter]::ToUInt32($bytes, $i + 18)
    $start = $i + 30 + $nameLength + [BitConverter]::ToUInt16($bytes, $i + 28)
    $inflate = New-Object System.IO.Compression.DeflateStream((New-Object System.IO.MemoryStream($bytes, $start, $compressed)), [System.IO.Compression.CompressionMode]::Decompress)
    $output = New-Object System.IO.MemoryStream
    $inflate.CopyTo($output)
    $dll = $output.ToArray()
}
if ($dll -eq $null) { throw "ReShade64.dll non trovato nell'installer di ReShade." }
$sha = [System.Security.Cryptography.SHA256]::Create()
if (([BitConverter]::ToString($sha.ComputeHash($dll))).Replace('-', '') -ne $dllHash) { throw "ReShade64.dll con hash inatteso." }
[System.IO.File]::WriteAllBytes((Join-Path $payload 'ReShade64.dll'), $dll)
Write-Host "Pacchetto pronto in $payload"

# --- Installer -----------------------------------------------------------------
& $iscc "/DAppVersion=$Version" "/DPayloadDir=$payload" "/DOutputDir=$dist" (Join-Path $root 'tools\installer\KarmaTV-PostFX.iss')
if ($LASTEXITCODE -ne 0) { throw "Inno Setup non e' riuscito a creare l'installer." }

# Impronta SHA-256: l'add-on la controlla prima di installare un aggiornamento
$setupExe = Join-Path $dist "KarmaTV-PostFX-Setup-$Version.exe"
$hash = (Get-FileHash $setupExe -Algorithm SHA256).Hash
"$hash  KarmaTV-PostFX-Setup-$Version.exe" | Set-Content -Path "$setupExe.sha256.txt" -Encoding ASCII
Write-Host ""
Write-Host "Installer creato: $setupExe"
Write-Host "Impronta: $setupExe.sha256.txt"
