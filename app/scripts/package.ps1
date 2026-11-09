param([switch]$IncludeAudio)
$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Root = Split-Path $AppRoot -Parent
$Licenses = Join-Path $AppRoot 'licenses'
$Files = @{
    'wxwidgets/licence.txt' = 'vendor/docs/licence.txt'
    'wxwidgets/lgpl.txt' = 'vendor/docs/lgpl.txt'
    'wxwidgets/gpl.txt' = 'vendor/docs/gpl.txt'
    'wxwidgets/libjpeg-README.txt' = 'vendor/src/jpeg/README'
    'wxwidgets/libpng-LICENSE.txt' = 'vendor/src/png/LICENSE'
    'wxwidgets/libtiff-COPYRIGHT.txt' = 'vendor/src/tiff/COPYRIGHT'
    'wxwidgets/zlib-LICENSE.txt' = 'vendor/src/zlib/LICENSE'
    'wxwidgets/pcre-LICENCE.txt' = 'vendor/3rdparty/pcre/LICENCE'
    'wxwidgets/pcre-COPYING.txt' = 'vendor/3rdparty/pcre/COPYING'
    'wxwidgets/nanosvg-LICENSE.txt' = 'vendor/3rdparty/nanosvg/LICENSE.txt'
    'wxwidgets/regex-COPYRIGHT.txt' = 'vendor/src/regex/COPYRIGHT'
    'wxwidgets/expat-COPYING.txt' = 'vendor/src/expat/expat/COPYING'
    'miniaudio-LICENSE.txt' = 'vendor/miniaudio/LICENSE'
    'nlohmann-json-LICENSE.txt' = 'vendor/nlohmann/LICENSE.MIT'
    'MinGW-w64-runtime.txt' = '.tools/w64devkit/COPYING.MinGW-w64-runtime.txt'
    'w64devkit-README.md' = '.tools/w64devkit/README.md'
}
foreach ($Entry in $Files.GetEnumerator()) {
    $Target = Join-Path $Licenses $Entry.Key
    New-Item -ItemType Directory -Force (Split-Path $Target -Parent) | Out-Null
    Copy-Item (Join-Path $AppRoot $Entry.Value) $Target -Force
}
if (!(Test-Path "$AppRoot/TolkeyenAudioguia.exe")) { throw 'Build the executable first.' }
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$Stage = Join-Path $AppRoot "dist/stage-$Stamp/Audioguia"
New-Item -ItemType Directory -Force "$Stage/app/config", "$Stage/app/docs" | Out-Null
foreach ($Language in @('0001-ESP','0002-ENG','0003-PORR')) {
    New-Item -ItemType Directory -Force "$Stage/audio/$Language" | Out-Null
}
Copy-Item "$AppRoot/TolkeyenAudioguia.exe" "$Stage/app/"
Copy-Item "$Root/LEEME.md", "$Root/Iniciar.cmd" "$Stage/"
Copy-Item "$AppRoot/README.md", "$AppRoot/vendor-versions.json" "$Stage/app/"
Copy-Item "$AppRoot/docs/*.md" "$Stage/app/docs/"
foreach ($Directory in @('screenshots','test-results')) {
    if (Test-Path "$AppRoot/docs/$Directory") { Copy-Item "$AppRoot/docs/$Directory" "$Stage/app/docs/" -Recurse }
}
if (Test-Path "$AppRoot/docs/visual-verification.json") { Copy-Item "$AppRoot/docs/visual-verification.json" "$Stage/app/docs/" }
Copy-Item $Licenses "$Stage/app/" -Recurse
Copy-Item "$AppRoot/config/audioguia.example.json" "$Stage/app/config/"
$Configuration = if ($IncludeAudio) { "$AppRoot/config/audioguia.json" } else { "$AppRoot/config/audioguia.example.json" }
Copy-Item $Configuration "$Stage/app/config/audioguia.json"
if ($IncludeAudio) {
    Get-ChildItem "$Root/audio" -Recurse -File | Where-Object { $_.Extension -in @('.mp3','.wav') } | ForEach-Object {
        $Relative = $_.FullName.Substring((Join-Path $Root 'audio').Length + 1)
        $Target = Join-Path "$Stage/audio" $Relative
        New-Item -ItemType Directory -Force (Split-Path $Target -Parent) | Out-Null
        Copy-Item $_.FullName $Target
    }
}
$Hash = (Get-FileHash "$Stage/app/TolkeyenAudioguia.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
$Manifest = [ordered]@{
    version = '0.1.0'
    architecture = 'x86 (SSE2)'
    built_with = 'w64devkit 2.11.0 / GCC 16.2.0'
    exe_sha256 = $Hash
    audio_included = [bool]$IncludeAudio
    created_utc = [DateTime]::UtcNow.ToString('o')
    compatibility = 'Windows 7 SP1/10 intended, not runtime-validated; Windows 11 tested as described in docs/testing.md'
}
[IO.File]::WriteAllText("$Stage/app/build-info.json", ($Manifest | ConvertTo-Json), (New-Object Text.UTF8Encoding($false)))
$Zip = Join-Path $AppRoot 'dist/TolkeyenAudioguia-x86-0.1.0.zip'
Compress-Archive -Path $Stage -DestinationPath $Zip -Force
Write-Host "Executable SHA256: $Hash"
Write-Host "Portable folder: $Stage"
Write-Host "ZIP: $Zip"
