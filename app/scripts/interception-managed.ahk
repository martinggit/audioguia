#Requires AutoHotkey v2.0
#SingleInstance Off
#NoTrayIcon
#Include %A_ScriptDir%\..\bridge\Lib\AutoHotInterception.ahk

DetectHiddenWindows(true)
if A_Args.Length < 3
    ExitApp(10)
Owner := Integer(A_Args[1])
Session := Integer(A_Args[2])
Settings := A_Args[3]
SplitPath(Settings, , &LocalDir)
LogPath := LocalDir "\bridge.log"
AHI := 0
ActiveId := 0
Held := Map()
Selector := 0
LastState := -1
Expected := IniRead(Settings, "Device", "Handle", "")
Message := DllCall("RegisterWindowMessageW", "Str", "TolkeyenAudioguia.DedicatedKey.v1", "UInt")
OwnerPid := WinGetPID("ahk_id " Owner)
OnExit(Cleanup)
OnError(Failed)
Log("Inicio del puente integrado. PID " DllCall("GetCurrentProcessId"))
Mutex := DllCall("CreateMutexW", "Ptr", 0, "Int", 0, "Str", "Local\TolkeyenAudioguia.GeniusBridge", "Ptr")
if !Mutex || A_LastError == 183 {
    Publish(6)
    Sleep(1200)
    ExitApp(6)
}
; The previous diagnostic version may predate the mutex. Do not compete with it.
if WinExist("Audioguia | Puente del Genius") || WinExist("Audioguia | Prueba de aislamiento del Genius") {
    Publish(6)
    Sleep(1200)
    ExitApp(6)
}
Publish(0)
AHI := AutoHotInterception()
SetTimer(Tick, 500)
Tick()
Persistent

Log(text) {
    global LogPath
    try {
        if FileExist(LogPath) && FileGetSize(LogPath) > 524288
            FileMove(LogPath, LogPath ".old", 1)
        FileAppend(FormatTime(, "yyyy-MM-dd HH:mm:ss") " " text "`r`n", LogPath, "UTF-8")
    }
}

Publish(state) {
    global Owner, LastState
    DllCall("SetPropW", "Ptr", Owner, "Str", "TolkeyenAudioguia.BridgeState.v1", "Ptr", state)
    DllCall("SetPropW", "Ptr", Owner, "Str", "TolkeyenAudioguia.BridgePulse.v1", "Ptr", DllCall("GetTickCount", "UInt"))
    if state != LastState {
        Log("Estado " state " (1 activo, 2 ausente, 3 ambiguo, 4 error, 5 sin configurar, 6 ocupado)")
        LastState := state
    }
}

OwnerAlive() {
    global Owner, OwnerPid, Session
    try {
        return DllCall("IsWindow", "Ptr", Owner) && WinGetPID("ahk_id " Owner) == OwnerPid
            && DllCall("GetPropW", "Ptr", Owner, "Str", "TolkeyenAudioguia.BridgeSession.v1", "Ptr") == Session
    } catch {
        return false
    }
}

Release() {
    global AHI, ActiveId, Held
    if AHI && ActiveId {
        AHI.UnsubscribeKeyboard(ActiveId)
        ActiveId := 0
        Held.Clear()
        Log("Captura liberada")
    }
}

Tick() {
    global Owner, AHI, Expected, ActiveId, Held, Selector
    if !OwnerAlive()
        ExitApp()
    if DllCall("RemovePropW", "Ptr", Owner, "Str", "TolkeyenAudioguia.BridgeConfigure.v1", "Ptr")
        Configure()
    if Selector {
        Publish(5)
        return
    }
    if Expected == "" {
        Publish(5)
        return
    }
    matches := []
    for id, device in AHI.GetDeviceList() {
        if !device.IsMouse && StrUpper(device.Handle) == StrUpper(Expected)
            matches.Push(id)
    }
    if matches.Length != 1 {
        Release()
        Publish(matches.Length ? 3 : 2)
        return
    }
    if ActiveId != matches[1] {
        Release()
        Held.Clear()
        ActiveId := matches[1]
        AHI.SubscribeKeyboard(ActiveId, true, KeyEvent)
        Log("Captura activa ID " ActiveId " " Expected)
    }
    Publish(1)
}

KeyEvent(code, state) {
    global Held, ActiveId, Owner, Session, Message
    if !ActiveId || !OwnerAlive()
        return
    if state == 0 {
        if Held.Has(code)
            Held.Delete(code)
        return
    }
    if state != 1 || Held.Has(code)
        return
    Held[code] := true
    ; Only the known physical keys are commands. All other keys of this
    ; keyboard interface remain blocked; no input is synthesized globally.
    switch code {
    case 82,79,80,81,75,76,77,71,72,73,284,309,55,74,78,83:
        if DllCall("GetPropW", "Ptr", Owner, "Str", "TolkeyenAudioguia.DedicatedReady.v1", "Ptr") != 1
            || !DllCall("IsWindowEnabled", "Ptr", Owner)
            return
        if !DllCall("PostMessageW", "Ptr", Owner, "UInt", Message, "UPtr", Session, "Ptr", code)
            Log("No se pudo enviar codigo " code)
    }
}

Configure() {
    global Selector, AHI, Expected
    if Selector {
        Selector.Show()
        return
    }
    Release()
    devices := []
    labels := []
    selected := 1
    for id, device in AHI.GetDeviceList() {
        if device.IsMouse || device.VID != 0x248A || device.PID != 0x8367
            || !InStr(StrUpper(device.Handle), "MI_01")
            continue
        devices.Push(device)
        labels.Push("ID " id " | " device.Handle)
        if StrUpper(device.Handle) == StrUpper(Expected)
            selected := devices.Length
    }
    Selector := Gui(, "Seleccionar teclado de Audioguia")
    Selector.AddText("w740", "Selecciona el Genius validado en el monitor, no el teclado principal."
        "`nEl receptor probado es VID_248A / PID_8367 / MI_01. La interfaz de mouse nunca se captura.")
    list := Selector.AddDropDownList("w740" (labels.Length ? " Choose" selected : ""), labels)
    if !labels.Length
        Selector.AddText("w740", "No se encontro un Genius compatible. Conecta el receptor y vuelve a abrir esta ventana.")
    save := Selector.AddButton("w180", "Guardar y capturar")
    cancel := Selector.AddButton("x+12 w150", "Cancelar")
    save.OnEvent("Click", (*) => SaveDevice(list, devices))
    cancel.OnEvent("Click", CloseSelector)
    Selector.OnEvent("Close", CloseSelector)
    Selector.Show()
}

SaveDevice(list, devices) {
    global Expected, Settings, AHI
    if list.Value < 1 || list.Value > devices.Length
        return
    chosen := devices[list.Value].Handle
    count := 0
    for id, device in AHI.GetDeviceList() {
        if !device.IsMouse && StrUpper(device.Handle) == StrUpper(chosen)
            count += 1
    }
    if count != 1 {
        MsgBox("El teclado falta o su identificador es ambiguo. Desconecta el duplicado y vuelve a configurar.")
        return
    }
    IniWrite(chosen, Settings, "Device", "Handle")
    Expected := chosen
    CloseSelector()
}

CloseSelector(*) {
    global Selector
    if Selector {
        Selector.Destroy()
        Selector := 0
    }
}

Failed(err, mode) {
    Log("ERROR: " err.Message)
    Publish(4)
    Release()
    ExitApp(4)
}

Cleanup(*) {
    global Mutex
    SetTimer(Tick, 0)
    Release()
    if IsSet(Mutex) && Mutex
        DllCall("CloseHandle", "Ptr", Mutex)
}
