# KarmaTV PostFX - installazione completa in rFactor 2.
#
# Installa in Bin64:
#   - ReShade 6.8.0 con supporto add-on (dxgi.dll), preso dall'installer ufficiale
#     di reshade.me e verificato con SHA-256;
#   - ReShade.ini preconfigurato (solo se non esiste gia');
#   - l'add-on KarmaTV_PostFX.addon64 e gli shader KarmaTV.
#
# Uso:
#   .\tools\install.ps1 -Rf2Bin64 "E:\SteamLibrary\steamapps\common\rFactor 2\Bin64"
#   .\tools\install.ps1 -Rf2Bin64 "..." -SkipReShade      (solo add-on e shader)
#   .\tools\install.ps1 -Rf2Bin64 "..." -Configuration Debug
#
# Dall'installer (KarmaTV-PostFX-Setup.exe) lo script gira con -Payload: tutti i file
# (add-on, shader, LUT, ReShade64.dll) vengono dalla cartella del pacchetto, senza internet.

param(
    [Parameter(Mandatory = $true)][string]$Rf2Bin64,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$SkipReShade,
    [switch]$Force,  # sostituisce un dxgi.dll che non e' ReShade (es. un'altra mod)
    [string]$Payload, # cartella del pacchetto dell'installer
    [string]$LogFile  # registro dell'installazione (usato dall'installer)
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if ($LogFile) { Start-Transcript -Path $LogFile -Force | Out-Null }
trap {
    Write-Host "ERRORE: $_"
    if ($LogFile) { Stop-Transcript | Out-Null }
    exit 1
}

# --- Versione di ReShade fissata ----------------------------------------------
$ReShadeVersion   = '6.8.0'
$ReShadeUrl       = "https://reshade.me/downloads/ReShade_Setup_${ReShadeVersion}_Addon.exe"
$ReShadeSetupHash = 'AFE4C8F13048306307983B8B3D41D5BF00A86820440B0E57DEA10950E1176445'
$ReShadeDllHash   = '0CEE63F9C9F13F3AC909C5B4903F4DBB4B719A7AB3B4F13B0DEAF83C814B94F7'

# --- Controlli iniziali -------------------------------------------------------
if (-not (Test-Path (Join-Path $Rf2Bin64 'rFactor2.exe'))) {
    throw "rFactor2.exe non trovato in '$Rf2Bin64': indica la cartella Bin64 di rFactor 2."
}
if (Get-Process -Name 'rFactor2' -ErrorAction SilentlyContinue) {
    throw "rFactor 2 e' in esecuzione: chiudilo prima di installare."
}

$addon = if ($Payload) { Join-Path $Payload 'KarmaTV_PostFX.addon64' } else { Join-Path $root "build\x64\$Configuration\KarmaTV_PostFX.addon64" }
$shaderSource = if ($Payload) { Join-Path $Payload 'shaders' } else { Join-Path $root 'shaders' }
$licenseSource = if ($Payload) { Join-Path $Payload 'licenses\ReShade-LICENSE.md' } else { Join-Path $root 'third_party\reshade\LICENSE.md' }
if (-not (Test-Path $addon)) {
    throw "Add-on non trovato: $addon. Compila prima con: cmake --preset x64; cmake --build --preset release"
}

$ktvDir = Join-Path $Rf2Bin64 'KarmaTV-PostFX'
New-Item -ItemType Directory -Force -Path $ktvDir | Out-Null
$manifestPath = Join-Path $ktvDir 'install-manifest.txt'
$manifest = [System.Collections.Generic.List[string]]::new()
if (Test-Path $manifestPath) { $manifest.AddRange([string[]](Get-Content $manifestPath)) }
function Add-Manifest([string]$entry) { if (-not $manifest.Contains($entry)) { $manifest.Add($entry) } }

# Imposta (o aggiunge) una chiave in una sezione di un file .ini, lasciando il resto invariato.
function Set-IniValue([string]$path, [string]$section, [string]$key, [string]$value) {
    $lines = [System.Collections.Generic.List[string]]::new()
    if (Test-Path $path) { $lines.AddRange([string[]](Get-Content $path)) }
    $sectionStart = -1
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Trim() -eq "[$section]") { $sectionStart = $i; break }
    }
    if ($sectionStart -lt 0) {
        if ($lines.Count -gt 0 -and $lines[$lines.Count - 1] -ne '') { $lines.Add('') }
        $lines.Add("[$section]")
        $lines.Add("$key=$value")
    }
    else {
        $insertAt = $lines.Count
        $done = $false
        for ($i = $sectionStart + 1; $i -lt $lines.Count; $i++) {
            if ($lines[$i].Trim().StartsWith('[')) { $insertAt = $i; break }
            if ($lines[$i] -match "^\s*$([regex]::Escape($key))\s*=") { $lines[$i] = "$key=$value"; $done = $true; break }
        }
        if (-not $done) {
            # Inserisce prima delle righe vuote finali della sezione
            while ($insertAt -gt $sectionStart + 1 -and $lines[$insertAt - 1].Trim() -eq '') { $insertAt-- }
            $lines.Insert($insertAt, "$key=$value")
        }
    }
    $lines | Set-Content -Path $path -Encoding ASCII
}

# Estrae un file dall'archivio ZIP accodato all'installer di ReShade
# (lettura diretta delle intestazioni locali: l'installer non viene mai eseguito).
function Expand-FromSetup([string]$setupPath, [string]$entryName) {
    $bytes = [System.IO.File]::ReadAllBytes($setupPath)
    for ($i = 0; $i -lt $bytes.Length - 30; $i++) {
        if ($bytes[$i] -ne 0x50 -or $bytes[$i + 1] -ne 0x4B -or $bytes[$i + 2] -ne 3 -or $bytes[$i + 3] -ne 4) { continue }
        $method = [BitConverter]::ToUInt16($bytes, $i + 8)
        $compressed = [BitConverter]::ToUInt32($bytes, $i + 18)
        $uncompressed = [BitConverter]::ToUInt32($bytes, $i + 22)
        $nameLength = [BitConverter]::ToUInt16($bytes, $i + 26)
        $extraLength = [BitConverter]::ToUInt16($bytes, $i + 28)
        $name = [System.Text.Encoding]::ASCII.GetString($bytes, $i + 30, $nameLength)
        if ($name -ne $entryName) { continue }
        if ($method -ne 8) { throw "Formato inatteso per $entryName nell'installer di ReShade." }

        $start = $i + 30 + $nameLength + $extraLength
        $source = New-Object System.IO.MemoryStream($bytes, $start, $compressed)
        $inflate = New-Object System.IO.Compression.DeflateStream($source, [System.IO.Compression.CompressionMode]::Decompress)
        $output = New-Object System.IO.MemoryStream
        $inflate.CopyTo($output)
        $result = $output.ToArray()
        if ($result.Length -ne $uncompressed) { throw "Estrazione di $entryName incompleta." }
        return , $result
    }
    throw "$entryName non trovato nell'installer di ReShade."
}

# --- ReShade -------------------------------------------------------------------
if (-not $SkipReShade) {
    $dxgi = Join-Path $Rf2Bin64 'dxgi.dll'
    $existingIsReShade = $false
    if (Test-Path $dxgi) {
        $existingIsReShade = (Get-Item $dxgi).VersionInfo.ProductName -match 'ReShade'
        if (-not $existingIsReShade -and -not $Force) {
            throw "In Bin64 c'e' gia' un dxgi.dll che non e' ReShade. Rimuovilo o rilancia con -Force (verra' salvato come dxgi.dll.ktv-backup)."
        }
    }

    $currentHash = if (Test-Path $dxgi) { (Get-FileHash $dxgi -Algorithm SHA256).Hash } else { '' }
    if ($currentHash -eq $ReShadeDllHash) {
        Write-Host "ReShade $ReShadeVersion e' gia' installato."
    }
    elseif ($Payload) {
        # ReShade incluso nel pacchetto dell'installer
        $dllBytes = [System.IO.File]::ReadAllBytes((Join-Path $Payload 'ReShade64.dll'))
    }
    else {
        # Download in una cartella dedicata, verificato con l'hash fissato sopra.
        $cache = Join-Path $env:LOCALAPPDATA 'KarmaTV-PostFX\cache'
        New-Item -ItemType Directory -Force -Path $cache | Out-Null
        $setup = Join-Path $cache "ReShade_Setup_${ReShadeVersion}_Addon.exe"
        if (-not (Test-Path $setup) -or (Get-FileHash $setup -Algorithm SHA256).Hash -ne $ReShadeSetupHash) {
            Write-Host "Scarico ReShade $ReShadeVersion da reshade.me ..."
            Invoke-WebRequest -Uri $ReShadeUrl -OutFile $setup -UseBasicParsing
        }
        if ((Get-FileHash $setup -Algorithm SHA256).Hash -ne $ReShadeSetupHash) {
            Remove-Item $setup -Force
            throw "L'installer di ReShade scaricato non corrisponde all'hash atteso: installazione annullata."
        }

        $dllBytes = Expand-FromSetup $setup 'ReShade64.dll'
    }
    if ($currentHash -ne $ReShadeDllHash) {
        $sha = [System.Security.Cryptography.SHA256]::Create()
        $dllHash = ([BitConverter]::ToString($sha.ComputeHash($dllBytes))).Replace('-', '')
        if ($dllHash -ne $ReShadeDllHash) { throw "ReShade64.dll estratto non corrisponde all'hash atteso." }

        if ((Test-Path $dxgi) -and -not $existingIsReShade) {
            Copy-Item $dxgi "$dxgi.ktv-backup" -Force
            Add-Manifest 'backup:dxgi.dll.ktv-backup'
            Write-Host "dxgi.dll esistente salvato come dxgi.dll.ktv-backup"
        }
        [System.IO.File]::WriteAllBytes($dxgi, $dllBytes)
        Write-Host "ReShade $ReShadeVersion installato come dxgi.dll"
    }
    Add-Manifest 'file:dxgi.dll'

    # Licenza di ReShade (BSD-3-Clause): va distribuita insieme alla DLL.
    $licenseDir = Join-Path $ktvDir 'licenses'
    New-Item -ItemType Directory -Force -Path $licenseDir | Out-Null
    Copy-Item $licenseSource (Join-Path $licenseDir 'ReShade-LICENSE.md') -Force

    # ReShade.ini: creato solo se manca, per non toccare configurazioni esistenti.
    $ini = Join-Path $Rf2Bin64 'ReShade.ini'
    if (Test-Path $ini) {
        Write-Host "ReShade.ini esistente lasciato invariato."
        # Dall'installer: un ReShade gia' presente deve comunque copiare la profondita'
        # prima delle cancellazioni, altrimenti nebbia, GTAO e DOF non funzionano.
        if ($Payload -and -not $manifest.Contains('file:ReShade.ini')) {
            if (-not (Test-Path "$ini.ktv-backup")) { Copy-Item $ini "$ini.ktv-backup" }
            Set-IniValue $ini 'DEPTH' 'DepthCopyBeforeClears' '1'
            Write-Host "ReShade.ini esistente: copia della profondita' attivata (originale in ReShade.ini.ktv-backup)."
        }
    }
    else {
        @(
            '[GENERAL]'
            'EffectSearchPaths=.\reshade-shaders\Shaders\**'
            'TextureSearchPaths=.\reshade-shaders\Textures\**'
            'PresetPath=.\KarmaTV-PostFX\ReShadePreset.ini'
            'PerformanceMode=0'
            ''
            '[INPUT]'
            'KeyOverlay=57,1,0,0'
            ''
            '[OVERLAY]'
            'TutorialProgress=4'
        ) | Set-Content -Path $ini -Encoding ASCII
        Add-Manifest 'file:ReShade.ini'
        Write-Host "Creato ReShade.ini (overlay su Home, preset in KarmaTV-PostFX)."
    }

    # Carattere dell'interfaccia: Barlow Condensed (licenza OFL, lo stesso di RF2 Server Manager),
    # copiato in KarmaTV-PostFX\fonts. Un add-on non puo' caricare caratteri propri, quindi si
    # imposta quello globale di ReShade. Solo se ReShade.ini e' stato creato da questo script.
    $fontSource = if ($Payload) { Join-Path $Payload 'fonts' } else { Join-Path $root 'third_party\fonts' }
    $fontDir = Join-Path $ktvDir 'fonts'
    New-Item -ItemType Directory -Force -Path $fontDir | Out-Null
    Copy-Item (Join-Path $fontSource '*') $fontDir -Force
    $font = Join-Path $fontDir 'BarlowCondensed-Medium.ttf'
    if ($manifest.Contains('file:ReShade.ini') -and (Test-Path $font)) {
        Set-IniValue $ini 'STYLE' 'Font' $font
        Set-IniValue $ini 'STYLE' 'FontSize' '21'
        Write-Host "Carattere dell'interfaccia impostato su Barlow Condensed."
    }

    # Depth buffer: rF2 lo cancella prima della fine del frame, quindi ReShade deve
    # copiarlo prima delle cancellazioni (come per Automobilista 2 nel database di ReShade).
    if ($manifest.Contains('file:ReShade.ini')) {
        Set-IniValue $ini 'DEPTH' 'DepthCopyBeforeClears' '1'
        Write-Host "Depth buffer: copia prima delle cancellazioni attivata."
    }

    # Overlay di ReShade (solo diagnostica) su Ctrl+9: Home in rF2 cambia telecamera, il 9 e' libero.
    if ($manifest.Contains('file:ReShade.ini')) {
        Set-IniValue $ini 'INPUT' 'KeyOverlay' '57,1,0,0'
        Write-Host "Overlay di ReShade spostato su Ctrl+9 (Home in rF2 cambia telecamera)."
    }

    # Preset ReShade: solo l'ordine di esecuzione; l'accensione la decide l'add-on.
    # L'ordine viene riscritto a ogni installazione (nuovi shader = nuova posizione).
    # AutoHDR deve restare l'ultimo: codifica l'immagine finale in PQ.
    $preset = Join-Path $ktvDir 'ReShadePreset.ini'
    $sorting = @(
        'KarmaTV_FadeSave@KarmaTV_01_Fade.fx'
        'KarmaTV_SMAA@KarmaTV_55_SMAA.fx'
        'KarmaTV_RTGI@KarmaTV_21_RTGI.fx'
        'KarmaTV_GTAO@KarmaTV_20_GTAO.fx'
        'KarmaTV_Sky@KarmaTV_12_Sky.fx'
        'KarmaTV_Fog@KarmaTV_10_Fog.fx'
        'KarmaTV_DOF@KarmaTV_65_DOF.fx'
        'KarmaTV_MotionBlur@KarmaTV_66_MotionBlur.fx'
        'KarmaTV_Bloom@KarmaTV_30_Bloom.fx'
        'KarmaTV_StarGlare@KarmaTV_35_StarGlare.fx'
        'KarmaTV_Tonemap@KarmaTV_40_Tonemap.fx'
        'KarmaTV_Grade@KarmaTV_50_Grade.fx'
        'KarmaTV_FSR1@KarmaTV_05_FSR1.fx'
        'KarmaTV_CAS@KarmaTV_60_CAS.fx'
        'KarmaTV_LensExtras@KarmaTV_80_LensExtras.fx'
        'KarmaTV_FadeMix@KarmaTV_01_Fade.fx'
        'KarmaTV_Debug@KarmaTV_99_Debug.fx'
        'KarmaTV_HDR@KarmaTV_95_HDR.fx'
    ) -join ','
    if (-not (Test-Path $preset)) {
        'Techniques=' | Set-Content -Path $preset -Encoding ASCII
    }
    $presetLines = [System.Collections.Generic.List[string]]::new()
    $presetLines.AddRange([string[]](Get-Content $preset))
    $index = $presetLines.FindIndex([Predicate[string]] { param($l) $l.StartsWith('TechniqueSorting=') })
    if ($index -ge 0) { $presetLines[$index] = "TechniqueSorting=$sorting" } else { $presetLines.Insert(1, "TechniqueSorting=$sorting") }
    $presetLines | Set-Content -Path $preset -Encoding ASCII
    Write-Host "Ordine degli shader KarmaTV aggiornato nel preset ReShade."
}

# --- Add-on e shader -----------------------------------------------------------
Copy-Item $addon -Destination $Rf2Bin64 -Force
Add-Manifest 'file:KarmaTV_PostFX.addon64'
Write-Host "Add-on copiato in $Rf2Bin64"

$shaderTarget = Join-Path $Rf2Bin64 'reshade-shaders\Shaders\KarmaTV'
New-Item -ItemType Directory -Force -Path $shaderTarget | Out-Null
# Shader KarmaTV non piu' presenti nel progetto (es. effetti eliminati): si tolgono anche da rF2.
$sourceShaders = Get-ChildItem $shaderSource -File | Select-Object -ExpandProperty Name
Get-ChildItem $shaderTarget -File -Filter 'KarmaTV_*' | Where-Object { $sourceShaders -notcontains $_.Name } | ForEach-Object {
    Remove-Item $_.FullName -Force
    Write-Host "Rimosso lo shader non piu' usato $($_.Name)"
}
Copy-Item (Join-Path $shaderSource '*') -Destination $shaderTarget -Force
Add-Manifest 'dir:reshade-shaders\Shaders\KarmaTV'
Write-Host "Shader copiati in $shaderTarget"

# Atlante delle LUT (preset integrati + luts\personale_1..4.png) generato da ktv_lut
$lutTool = Join-Path $root "build\x64\$Configuration\ktv_lut.exe"
$textureTarget = Join-Path $Rf2Bin64 'reshade-shaders\Textures\KarmaTV'
New-Item -ItemType Directory -Force -Path $textureTarget | Out-Null
if ($Payload) {
    Copy-Item (Join-Path $Payload 'textures\KarmaTV_LUTs.png') -Destination $textureTarget -Force
    Add-Manifest 'dir:reshade-shaders\Textures\KarmaTV'
    Write-Host "Atlante delle LUT copiato."
}
elseif (Test-Path $lutTool) {
    & $lutTool atlas (Join-Path $textureTarget 'KarmaTV_LUTs.png') (Join-Path $root 'luts') | ForEach-Object { Write-Host $_ }
    Add-Manifest 'dir:reshade-shaders\Textures\KarmaTV'
}
else {
    Write-Warning "ktv_lut.exe non trovato: l'atlante delle LUT non e' stato aggiornato."
}

$manifest | Set-Content -Path $manifestPath -Encoding UTF8
Write-Host ""
Write-Host "Fatto. Avvia rF2, entra in pista e premi Canc per aprire la finestra KarmaTV PostFX."
if (-not $Payload) { Write-Host "Per rimuovere tutto: .\tools\uninstall.ps1 -Rf2Bin64 `"$Rf2Bin64`"" }
if ($LogFile) { Stop-Transcript | Out-Null }
