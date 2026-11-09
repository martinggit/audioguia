$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Tools = Join-Path $AppRoot '.tools'
$Vendor = Join-Path $AppRoot 'vendor'
New-Item -ItemType Directory -Force $Tools, $Vendor | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Fetch($Url, $Path, $Hash) {
    $Candidate = $Path
    if (!(Test-Path $Path)) {
        Write-Host "Downloading $Url"
        $Candidate = "$Path.partial"
        Invoke-WebRequest -UseBasicParsing $Url -OutFile $Candidate
    }
    $Actual = (Get-FileHash -Algorithm SHA256 $Candidate).Hash.ToLowerInvariant()
    if ($Hash -and $Actual -ne $Hash) { throw "SHA256 mismatch: $Path ($Actual)" }
    if ($Candidate -ne $Path) { Move-Item -LiteralPath $Candidate -Destination $Path }
    Write-Host "$Actual  $Path"
}

$KitArchive = Join-Path $Tools 'w64devkit-x86-2.11.0.7z.exe'
Fetch 'https://github.com/skeeto/w64devkit/releases/download/v2.11.0/w64devkit-x86-2.11.0.7z.exe' $KitArchive 'f7e8c79eacc4cbdd718a4cbb6519f2f370311017dc08f7750abedb4340d1b65d'
if (!(Test-Path "$Tools/w64devkit/bin/g++.exe")) {
    $Process = Start-Process -FilePath $KitArchive -ArgumentList @('-y', ('-o"{0}"' -f $Tools)) -Wait -PassThru
    if ($Process.ExitCode -ne 0) { throw 'Toolchain extraction failed' }
}
$CMake = Join-Path $Tools 'w64devkit/bin/cmake.exe'
$WxArchive = Join-Path $Tools 'wxWidgets-3.2.12.zip'
Fetch 'https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.zip' $WxArchive '1f7ec935dddbba5cf454795dc255ef1e21c668533f5a5951a6034f2949c80433'
if (!(Test-Path "$Vendor/CMakeLists.txt")) {
    Push-Location $Vendor
    try { & $CMake -E tar xf $WxArchive; if ($LASTEXITCODE) { throw 'wx extraction failed' } }
    finally { Pop-Location }
}
New-Item -ItemType Directory -Force "$Vendor/miniaudio", "$Vendor/nlohmann" | Out-Null
Fetch 'https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/miniaudio.h' "$Vendor/miniaudio/miniaudio.h" '6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e'
Fetch 'https://raw.githubusercontent.com/mackron/miniaudio/0.11.26/LICENSE' "$Vendor/miniaudio/LICENSE" '457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb'
Fetch 'https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp' "$Vendor/nlohmann/json.hpp" 'aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63'
Fetch 'https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT' "$Vendor/nlohmann/LICENSE.MIT" '46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603'
$Root = Split-Path $AppRoot -Parent
foreach ($Language in @('0001-ESP','0002-ENG','0003-PORT')) {
    New-Item -ItemType Directory -Force (Join-Path $Root "audio/$Language") | Out-Null
}
if (!(Test-Path "$AppRoot/config/audioguia.json")) {
    Copy-Item "$AppRoot/config/audioguia.example.json" "$AppRoot/config/audioguia.json"
}
Write-Host 'Portable dependencies ready. Run scripts/build.ps1.'
