param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$Identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$Principal = New-Object Security.Principal.WindowsPrincipal($Identity)
if (!$Principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Abra PowerShell como administrador para instalar o desinstalar el controlador.'
}
$AppRoot = Split-Path $PSScriptRoot -Parent
$Installer = Join-Path $AppRoot '.tools/numpad/Interception/Interception/command line installer/install-interception.exe'
if (Test-Path "$AppRoot/bridge/installer/command line installer/install-interception.exe") {
    $Installer = "$AppRoot/bridge/installer/command line installer/install-interception.exe"
}
if (!(Test-Path -LiteralPath $Installer)) { throw "No se encuentra el instalador oficial: $Installer" }
$Argument = if ($Uninstall) { '/uninstall' } else { '/install' }
& $Installer $Argument
if ($LASTEXITCODE -ne 0) { throw "El instalador termino con codigo $LASTEXITCODE" }
Write-Host 'Revise el resultado del instalador. Guarde su trabajo y reinicie Windows antes de probar.'
