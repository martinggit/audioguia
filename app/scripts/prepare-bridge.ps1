$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Source = Join-Path $AppRoot '.tools/numpad'
$Target = Join-Path $AppRoot 'bridge'
$Packages = @(
    @{ Name='Interception'; Url='https://github.com/oblitum/Interception/releases/download/v1.0.1/Interception.zip'; Hash='AD038963D6413055765128B0B931F6E765147C9916DBA79E65D872B261F9AF10' },
    @{ Name='AutoHotInterception'; Url='https://github.com/evilC/AutoHotInterception/releases/download/v0.9.2/AutoHotInterception.zip'; Hash='5E0DD6C69A61FE0E6F040D9FD82D7099022A84A0E6BC3A338666EEB23CA4F0DA' },
    @{ Name='AutoHotkey'; Url='https://github.com/AutoHotkey/AutoHotkey/releases/download/v2.0.31/AutoHotkey_2.0.31.zip'; Hash='CA1A595ACA4528755D8D4CD7FED23D497A37BD9A0B6892072612D68DEE1BD00A' }
)
$ProgressPreference = 'SilentlyContinue'
New-Item -ItemType Directory -Force $Source | Out-Null
foreach ($Package in $Packages) {
    $Archive = Join-Path $Source ($Package.Name + '.zip')
    if (!(Test-Path $Archive)) {
        Invoke-WebRequest -UseBasicParsing -Uri $Package.Url -OutFile "$Archive.partial"
        if ((Get-FileHash "$Archive.partial" -Algorithm SHA256).Hash -ne $Package.Hash) { throw "SHA256 incorrecto: $($Package.Name)" }
        Move-Item "$Archive.partial" $Archive
    }
    if ((Get-FileHash $Archive -Algorithm SHA256).Hash -ne $Package.Hash) { throw "SHA256 incorrecto: $Archive" }
    Expand-Archive -LiteralPath $Archive -DestinationPath (Join-Path $Source $Package.Name) -Force
}
New-Item -ItemType Directory -Force "$Target/Lib", "$Target/AutoHotkey", "$Target/licenses", "$Target/installer" | Out-Null
Copy-Item "$Source/AutoHotInterception/AHK v2/Lib/*" "$Target/Lib" -Recurse -Force
Copy-Item "$Source/AutoHotInterception/Common/Lib/AutoHotInterception.dll" "$Target/Lib" -Force
foreach ($Arch in @('x86','x64')) {
    Copy-Item "$Source/Interception/Interception/library/$Arch/interception.dll" "$Target/Lib/$Arch" -Force
}
Copy-Item "$Source/AutoHotkey/AutoHotkey32.exe", "$Source/AutoHotkey/AutoHotkey64.exe" "$Target/AutoHotkey" -Force
Copy-Item "$Source/AutoHotInterception/LICENSE" "$Target/licenses/AutoHotInterception.txt" -Force
Copy-Item "$Source/AutoHotkey/license.txt" "$Target/licenses/AutoHotkey.txt" -Force
Copy-Item "$Source/Interception/Interception/*" "$Target/installer" -Recurse -Force
Write-Host "Puente preparado: $Target"
