param([string]$DeviceHandle = '', [switch]$SimulateDevice)
$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Fixture = Join-Path $AppRoot ('build/dedicated-input-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$Fixture/app/config" | Out-Null
Copy-Item "$AppRoot/TolkeyenAudioguia.exe" "$Fixture/app/"
Copy-Item "$AppRoot/config/audioguia.example.json" "$Fixture/app/config/audioguia.json"
New-Item -ItemType Directory -Force "$Fixture/app/scripts" | Out-Null
Copy-Item "$AppRoot/scripts/interception-managed.ahk" "$Fixture/app/scripts/"
Copy-Item "$AppRoot/bridge" "$Fixture/app/" -Recurse
if ($SimulateDevice) {
    $DeviceHandle = 'HID\VID_248A&PID_8367&REV_0100&MI_01'
    Copy-Item "$AppRoot/tests/bridge_fake.ahk" "$Fixture/app/bridge/Lib/AutoHotInterception.ahk" -Force
}
$Settings = Join-Path $Fixture 'local/dedicated.ini'
New-Item -ItemType Directory -Force (Split-Path $Settings -Parent) | Out-Null
Set-Content -LiteralPath $Settings -Value "[Settings]`r`nEnabled=0`r`n[Device]`r`nHandle=$DeviceHandle" -Encoding Unicode
$PreviousSettings = $env:AUDIOGUIA_BRIDGE_SETTINGS
$env:AUDIOGUIA_BRIDGE_SETTINGS = $Settings
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DedicatedInputTest {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern bool WritePrivateProfileString(string section, string key, string value, string file);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern uint RegisterWindowMessage(string name);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr h, string name);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int mode);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
}
'@
$Process = $null
try {
    $Process = Start-Process "$Fixture/app/TolkeyenAudioguia.exe" -PassThru
    $Deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 100
        $Process.Refresh()
        if ($Process.HasExited) { throw 'La app de prueba termino antes de mostrar la ventana.' }
        $Window = $Process.MainWindowHandle
    } while ($Window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $Deadline)
    if ($Window -eq [IntPtr]::Zero) { throw 'No se encontro la ventana de prueba.' }
    $Root = [System.Windows.Automation.AutomationElement]::FromHandle($Window)
    $Condition = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::NameProperty, 'Botonera dedicada')
    $CheckBox = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $Condition)
    if (!$CheckBox) { throw 'No se encontro el selector de botonera dedicada.' }
    $CheckHandle = [IntPtr]$CheckBox.Current.NativeWindowHandle
    if ($CheckHandle -eq [IntPtr]::Zero) { throw 'El selector no tiene control nativo.' }
    $Message = [DedicatedInputTest]::RegisterWindowMessage('TolkeyenAudioguia.DedicatedKey.v1')
    function Read-Preferences { (Get-Content "$Fixture/app/config/audioguia.json" -Raw | ConvertFrom-Json).preferences }
    function Send-Scan([int]$Scan, [long]$Version=1) {
        if (![DedicatedInputTest]::PostMessage($Window,$Message,[IntPtr]$Version,[IntPtr]$Scan)) { throw 'PostMessage fallo.' }
        Start-Sleep -Milliseconds 250
    }
    function Wait-State([int]$Expected) {
        $Until = [DateTime]::UtcNow.AddSeconds(20)
        do {
            Start-Sleep -Milliseconds 100
            $Current = [DedicatedInputTest]::GetProp($Window,'TolkeyenAudioguia.BridgeState.v1').ToInt64()
        } while ($Current -ne $Expected -and [DateTime]::UtcNow -lt $Until)
        if ($Current -ne $Expected) { throw "Estado esperado $Expected, recibido $Current. Registro: $(Split-Path $Settings -Parent)" }
    }
    $Initial = Read-Preferences
    Send-Scan 309
    if ((Read-Preferences).direction -ne $Initial.direction) { throw 'Acepto una orden sin habilitacion.' }
    [void][DedicatedInputTest]::SendMessage($CheckHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    if ([DedicatedInputTest]::GetProp($Window,'TolkeyenAudioguia.DedicatedReady.v1').ToInt64() -ne 1) {
        throw 'La app no habilito el protocolo.'
    }
    [void][DedicatedInputTest]::ShowWindow($Window,6)
    Start-Sleep -Milliseconds 200
    $Foreground = [DedicatedInputTest]::GetForegroundWindow()
    if ($Foreground -eq $Window) { throw 'La app no quedo en segundo plano.' }
    $Deadline = [DateTime]::UtcNow.AddSeconds(20)
    $ExpectedState = if ($DeviceHandle) { 1 } else { 5 }
    do {
        Start-Sleep -Milliseconds 100
        $State = [DedicatedInputTest]::GetProp($Window,'TolkeyenAudioguia.BridgeState.v1').ToInt64()
    } while ($State -ne $ExpectedState -and [DateTime]::UtcNow -lt $Deadline)
    if ($State -ne $ExpectedState) { throw "El puente no llego al estado esperado $ExpectedState. Registro: $(Split-Path $Settings -Parent)" }
    $Session = [DedicatedInputTest]::GetProp($Window,'TolkeyenAudioguia.BridgeSession.v1').ToInt64()
    if (!$Session) { throw 'No hay sesion del puente.' }
    Send-Scan 309 1
    if ((Read-Preferences).direction -ne $Initial.direction) { throw 'Acepto una version invalida.' }
    if (!$DeviceHandle) {
        Send-Scan 309 $Session
        if ((Read-Preferences).direction -ne $Initial.direction) { throw 'Acepto una orden sin dispositivo configurado.' }
        Write-Host 'PASS: inicio supervisado, estado sin configurar y rechazo de ordenes sin captura.'
        return
    }
    Send-Scan 309 $Session
    if ((Read-Preferences).direction -eq $Initial.direction) { throw 'No cambio el recorrido en segundo plano.' }
    Send-Scan 55 $Session
    if ((Read-Preferences).mode -eq $Initial.mode) { throw 'No cambio el modo.' }
    Send-Scan 74 $Session
    if ([Math]::Abs((Read-Preferences).volume - ([Math]::Max([double]0,[double]($Initial.volume-0.05)))) -gt 0.001) {
        throw 'No ajusto el volumen.'
    }
    $BeforeUnknown = Get-Content "$Fixture/app/config/audioguia.json" -Raw
    Send-Scan 325 $Session
    if ((Get-Content "$Fixture/app/config/audioguia.json" -Raw) -ne $BeforeUnknown) { throw 'Proceso codigo desconocido.' }
    if ([DedicatedInputTest]::GetForegroundWindow() -ne $Foreground) { throw 'La app robo el foco.' }
    if ($SimulateDevice) {
        [void][DedicatedInputTest]::ShowWindow($Window,9)
        $ConfigureCondition = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, 'Configurar botonera')
        $Configure = $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $ConfigureCondition)
        [void][DedicatedInputTest]::SendMessage([IntPtr]$Configure.Current.NativeWindowHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
        Wait-State 5
        $Selector = [DedicatedInputTest]::FindWindow('AutoHotkeyGUI','Seleccionar teclado de Audioguia')
        if ($Selector -eq [IntPtr]::Zero) { throw 'No se abrio la seleccion de dispositivo.' }
        if (![DedicatedInputTest]::IsWindowVisible($Selector)) { throw 'La seleccion de dispositivo esta oculta.' }
        $SelectorRoot = [System.Windows.Automation.AutomationElement]::FromHandle($Selector)
        $SaveCondition = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::NameProperty, 'Guardar y capturar')
        $Save = $SelectorRoot.FindFirst([System.Windows.Automation.TreeScope]::Descendants,$SaveCondition)
        [void][DedicatedInputTest]::SendMessage([IntPtr]$Save.Current.NativeWindowHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
        Wait-State 1
        if ((Get-Content $Settings -Raw) -notmatch [regex]::Escape($DeviceHandle)) { throw 'No guardo la seleccion local.' }
        Write-Host 'PASS: seleccion explicita y guardado local del receptor.'
        foreach ($Count in @(0,2,1)) {
            [void][DedicatedInputTest]::WritePrivateProfileString('Test','Devices',"$Count",$Settings)
            $Expected = if ($Count -eq 0) { 2 } elseif ($Count -eq 2) { 3 } else { 1 }
            Wait-State $Expected
            if ($Count -ne 1) {
                $Before = Get-Content "$Fixture/app/config/audioguia.json" -Raw
                Start-Sleep -Milliseconds 100
                Send-Scan 309 $Session
                if ((Get-Content "$Fixture/app/config/audioguia.json" -Raw) -ne $Before) { throw 'Acepto orden sin captura unica.' }
            }
        }
        Write-Host 'PASS: ausencia, ambiguedad y reconexion automatica simuladas sin reenviar ordenes.'
    }
    [void][DedicatedInputTest]::ShowWindow($Window,9)
    [void][DedicatedInputTest]::SendMessage($CheckHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    if ([DedicatedInputTest]::GetProp($Window,'TolkeyenAudioguia.BridgeSession.v1').ToInt64()) { throw 'No cerro la sesion al desactivar.' }
    $BeforeDisabled = Get-Content "$Fixture/app/config/audioguia.json" -Raw
    Send-Scan 309 $Session
    if ((Get-Content "$Fixture/app/config/audioguia.json" -Raw) -ne $BeforeDisabled) { throw 'Acepto una orden atrasada.' }
    [void][DedicatedInputTest]::SendMessage($CheckHandle,0x00F5,[IntPtr]::Zero,[IntPtr]::Zero)
    Wait-State 1
    [void]$Process.CloseMainWindow()
    if (!$Process.WaitForExit(5000)) { throw 'No cerro la app para la prueba de persistencia.' }
    $Process = Start-Process "$Fixture/app/TolkeyenAudioguia.exe" -PassThru
    $Until = [DateTime]::UtcNow.AddSeconds(20)
    do { Start-Sleep -Milliseconds 100; $Process.Refresh(); $Window=$Process.MainWindowHandle }
    while ($Window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $Until)
    Wait-State 1
    $BeforeRestart = Get-Content "$Fixture/app/config/audioguia.json" -Raw
    Send-Scan 309 $Session
    if ((Get-Content "$Fixture/app/config/audioguia.json" -Raw) -ne $BeforeRestart) { throw 'Acepto sesion del proceso anterior.' }
    Write-Host 'PASS: opt-in, sesion, recorrido/modo/volumen en segundo plano, foco, cierre y arranque automatico persistente.'
    Write-Host "Configuracion aislada: $Fixture"
} finally {
    if ($Process -and !$Process.HasExited) {
        [void]$Process.CloseMainWindow()
        if (!$Process.WaitForExit(5000)) { throw 'La instancia de prueba no cerro normalmente.' }
    }
    $env:AUDIOGUIA_BRIDGE_SETTINGS = $PreviousSettings
}
