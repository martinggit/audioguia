; Isolated bridge lifecycle tests only. Never used by production packaging.
class AutoHotInterception {
    GetDeviceList() {
        global Settings
        count := Integer(IniRead(Settings, "Test", "Devices", "1"))
        result := Map()
        Loop count
            result[A_Index] := {ID: A_Index, VID: 0x248A, PID: 0x8367, IsMouse: false,
                Handle: "HID\VID_248A&PID_8367&REV_0100&MI_01"}
        return result
    }
    SubscribeKeyboard(id, block, callback, concurrent := false) {
    }
    UnsubscribeKeyboard(id) {
    }
}
