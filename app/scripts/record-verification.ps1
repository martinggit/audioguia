$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Shots = Join-Path $AppRoot 'docs/screenshots'
$Logs = Join-Path $AppRoot 'docs/test-results'
New-Item -ItemType Directory -Force $Shots, $Logs | Out-Null
Add-Type -AssemblyName System.Drawing
$Names = @('main','compact','compact-paused','preferences','missing-audio')
$Captures = @()
foreach ($Name in $Names) {
    $Source = Join-Path $AppRoot "build/ui-smoke/evidence-unicode/$Name.png"
    $Destination = Join-Path $Shots "$Name.png"
    Copy-Item $Source $Destination -Force
    $Image = [Drawing.Image]::FromFile($Destination)
    try {
        $Captures += [ordered]@{file="screenshots/$Name.png";width=$Image.Width;height=$Image.Height;sha256=(Get-FileHash $Destination -Algorithm SHA256).Hash.ToLowerInvariant()}
    } finally { $Image.Dispose() }
}
Copy-Item "$AppRoot/build/ui-smoke/evidence-unicode/results.txt" "$Logs/windows-mp3-unicode.txt" -Force
Copy-Item "$AppRoot/build/ui-smoke/evidence/results.txt" "$Logs/windows-wav.txt" -Force
Copy-Item "$AppRoot/build/windows-x86-release/Testing/Temporary/LastTest.log" "$Logs/windows-core-controller-keyboard.log" -Force
$Reference = Join-Path $env:LOCALAPPDATA 'Temp/codex-clipboard-27e66ca6-2307-4dec-ae82-4dd8d73cb971.png'
if (Test-Path $Reference) { Copy-Item $Reference "$Shots/reference-boceto.png" -Force }
$Sources = @(Get-ChildItem "$AppRoot/src" -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{file=$_.FullName.Substring($AppRoot.Length+1).Replace('\','/');sha256=(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
})
$Receipt = [ordered]@{
    captured_utc = [DateTime]::UtcNow.ToString('o')
    platform = 'Windows 11 25H2 x64, native x86 wxWidgets process'
    capture = 'PrintWindow with a DPI-aware capture process'
    executable_sha256 = (Get-FileHash "$AppRoot/TolkeyenAudioguia.exe" -Algorithm SHA256).Hash.ToLowerInvariant()
    comparison = 'Inspected against supplied hand-drawn reference; native controls, no independent logo, not a pixel-perfect redraw'
    captures = $Captures
    sources = $Sources
}
[IO.File]::WriteAllText("$AppRoot/docs/visual-verification.json", ($Receipt | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding($false)))
Write-Host "Recorded $($Captures.Count) captures and $($Sources.Count) source hashes."
