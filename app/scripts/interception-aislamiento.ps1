param([switch]$Audio)
$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Arch = if ([Environment]::Is64BitOperatingSystem) { '64' } else { '32' }
$Ahk = Join-Path $AppRoot ".tools/numpad/AutoHotkey/AutoHotkey$Arch.exe"
$Script = Join-Path $PSScriptRoot 'interception-aislamiento.ahk'
if (!(Test-Path -LiteralPath $Ahk)) { throw "No se encuentra AutoHotkey portable: $Ahk" }
$Arguments = '"' + $Script + '"'
if ($Audio) { $Arguments += ' --audio' }
Start-Process -FilePath $Ahk -ArgumentList $Arguments -WorkingDirectory $PSScriptRoot
