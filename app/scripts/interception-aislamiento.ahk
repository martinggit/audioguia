#Requires AutoHotkey v2.0
#SingleInstance Ignore
#Include %A_ScriptDir%\..\.tools\numpad\AutoHotInterception\AHK v2\Lib\AutoHotInterception.ahk

; Default: diagnostic. --audio: explicit commands to the opted-in app window.
; Never inject keyboard input or activate the target window.
AudioMode := A_Args.Length == 1 && A_Args[1] == "--audio"
if AudioMode {
    MsgBox("El puente manual de audio fue reemplazado. Abre Audioguia y marca Botonera dedicada; usa Configurar botonera la primera vez.")
    ExitApp()
}
TargetWindow := 0
TargetPid := 0
CommandMessage := DllCall("RegisterWindowMessageW", "Str", "TolkeyenAudioguia.DedicatedKey.v1", "UInt")
ExpectedHandle := "HID\VID_248A&PID_8367&REV_0100&MI_01"
Actions := Map(79, "Punto 1", 80, "Punto 2", 81, "Punto 3",
    75, "Punto 4", 76, "Punto 5", 77, "Punto 6",
    71, "Punto 7", 72, "Punto 8", 73, "9: sin punto asignado",
    82, "Detener", 284, "Pausar / reanudar", 309, "Cambiar recorrido",
    55, "Cambiar modo", 74, "Volumen -", 78, "Volumen +", 83, "Idioma Simple")
Held := Map()
ActiveId := 0
PressCount := 0
Rows := []
AHI := AutoHotInterception()
BridgeMutex := DllCall("CreateMutexW", "Ptr", 0, "Int", 0, "Str", "Local\TolkeyenAudioguia.GeniusBridge", "Ptr")
if !BridgeMutex || A_LastError == 183 {
    MsgBox("Ya hay otro puente del Genius. Cierra la captura anterior antes de iniciar una prueba.")
    ExitApp(6)
}

Window := Gui(, AudioMode ? "Audioguia | Puente del Genius" : "Audioguia | Prueba de aislamiento del Genius")
Window.SetFont("s10", "Segoe UI")
Window.AddText("w720", (AudioMode ? "CONTROL DE AUDIO EN SEGUNDO PLANO. " : "Prueba sin audio. ")
    "Solo se captura el teclado Genius 248A:8367, interfaz MI_01.")
Status := Window.AddText("w720", "INACTIVO: el Genius funciona como un teclado normal.")
StartButton := Window.AddButton("w220", "Activar captura del Genius")
StopButton := Window.AddButton("x+10 w220", "Liberar Genius")
ExitButton := Window.AddButton("x+10 w220", "Salir")
LogBox := Window.AddEdit("xm w720 r16 ReadOnly", "")
Window.AddText("w720", AudioMode
    ? "Activa Botonera dedicada en Audioguia antes de capturar. Minimiza las ventanas, no cierres la app."
    : "Con captura activa: escribe en Bloc de notas con ambos teclados. El Genius no debe escribir.")
StartButton.OnEvent("Click", StartCapture)
StopButton.OnEvent("Click", StopCapture)
ExitButton.OnEvent("Click", (*) => ExitApp())
Window.OnEvent("Close", (*) => ExitApp())
OnExit(Cleanup)
Window.Show()

MatchingDevices() {
    global AHI, ExpectedHandle
    matches := []
    for id, device in AHI.GetDeviceList() {
        if !device.IsMouse && device.VID == 0x248A && device.PID == 0x8367
            && StrUpper(device.Handle) == StrUpper(ExpectedHandle)
            matches.Push(id)
    }
    return matches
}

LogLine(text) {
    global Rows, LogBox
    Rows.Push(FormatTime(, "HH:mm:ss") " " text)
    if Rows.Length > 100
        Rows.RemoveAt(1)
    output := ""
    for row in Rows
        output .= row "`r`n"
    LogBox.Value := output
    SendMessage(0x115, 7, 0, LogBox.Hwnd) ; Scroll this log only; do not change focus.
}

StartCapture(*) {
    global AHI, ActiveId, Held, Status, StartButton, AudioMode, TargetWindow, TargetPid, CommandMessage
    if ActiveId
        return
    try {
        matches := MatchingDevices()
        if matches.Length != 1
            throw Error("Se esperaba un solo Genius compatible; encontrados: " matches.Length)
        if AudioMode {
            if !CommandMessage
                throw Error("No se pudo registrar el protocolo de ordenes.")
            targets := []
            for hwnd in WinGetList("ahk_exe TolkeyenAudioguia.exe") {
                if DllCall("GetPropW", "Ptr", hwnd, "Str", "TolkeyenAudioguia.DedicatedReady.v1", "Ptr")
                    targets.Push(hwnd)
            }
            if targets.Length != 1
                throw Error("Abre una sola instancia de la nueva Audioguia y activa Botonera dedicada.")
            TargetWindow := targets[1]
            TargetPid := WinGetPID("ahk_id " TargetWindow)
            if !TargetReady()
                throw Error("Activa Botonera dedicada y cierra los dialogos de configuracion.")
        }
        Held.Clear()
        ActiveId := matches[1]
        AHI.SubscribeKeyboard(ActiveId, true, KeyEvent)
        Status.Text := "CAPTURA ACTIVA: Genius ID " ActiveId " bloqueado para otras aplicaciones."
        StartButton.Enabled := false
        LogLine(AudioMode ? "Puente activo: el Genius controla Audioguia sin cambiar el foco."
            : "Captura activada. Cambia a Bloc de notas para probar.")
        SetTimer(CheckDevice, 1000)
    } catch as err {
        StopCapture()
        LogLine("ERROR: " err.Message)
    }
}

StopCapture(*) {
    global AHI, ActiveId, Held, Status, StartButton
    SetTimer(CheckDevice, 0)
    if ActiveId {
        AHI.UnsubscribeKeyboard(ActiveId)
        ActiveId := 0
        LogLine("Genius liberado: vuelve a escribir normalmente.")
    }
    Held.Clear()
    StartButton.Enabled := true
    Status.Text := "INACTIVO: el Genius funciona como un teclado normal."
}

KeyEvent(code, state) {
    global ActiveId, Held, Actions, PressCount, AudioMode, TargetWindow, CommandMessage
    if !ActiveId
        return
    if state == 0 {
        if Held.Has(code)
            Held.Delete(code)
        LogLine("UP codigo=" code)
        return
    }
    if state != 1 || Held.Has(code)
        return
    Held[code] := true
    PressCount += 1
    action := Actions.Has(code) ? Actions[code] : "Sin asignar (bloqueada)"
    LogLine("DOWN #" PressCount " codigo=" code " -> " action)
    if AudioMode && Actions.Has(code) {
        if !TargetReady() {
            LogLine("DESCARTADA: app cerrada, botonera desactivada o dialogo abierto. El Genius sigue bloqueado.")
            return
        }
        if !DllCall("PostMessageW", "Ptr", TargetWindow, "UInt", CommandMessage, "UPtr", 1, "Ptr", code)
            LogLine("ERROR: Windows no permitio enviar la orden. Ejecuta app y puente con los mismos permisos.")
    }
}

TargetReady() {
    global TargetWindow, TargetPid
    if !TargetWindow || !DllCall("IsWindow", "Ptr", TargetWindow)
        return false
    try {
        return WinGetPID("ahk_id " TargetWindow) == TargetPid
            && DllCall("GetPropW", "Ptr", TargetWindow, "Str", "TolkeyenAudioguia.DedicatedReady.v1", "Ptr") == 1
            && DllCall("IsWindowEnabled", "Ptr", TargetWindow)
    } catch {
        return false
    }
}

CheckDevice() {
    global ActiveId
    try {
        matches := MatchingDevices()
        if matches.Length != 1 || matches[1] != ActiveId {
            StopCapture()
            LogLine("Cambio de dispositivo: captura detenida. Revisa el receptor antes de reactivar.")
        }
    } catch as err {
        StopCapture()
        LogLine("ERROR de dispositivo: " err.Message)
    }
}

Cleanup(*) {
    global AHI, ActiveId, BridgeMutex
    SetTimer(CheckDevice, 0)
    if ActiveId
        AHI.UnsubscribeKeyboard(ActiveId)
    if BridgeMutex
        DllCall("CloseHandle", "Ptr", BridgeMutex)
}
