param([switch]$NoAudio, [string]$Mp3Fixture = '', [switch]$UnicodeRoot)
$ErrorActionPreference = 'Stop'
$AppRoot = Split-Path $PSScriptRoot -Parent
$Fixture = Join-Path $AppRoot 'build/ui-smoke/Audioguia'
$Evidence = Join-Path $AppRoot 'build/ui-smoke/evidence'
if ($UnicodeRoot) {
    $Fixture = Join-Path $AppRoot ('build/ui-smoke/Audioguia con espacios ' + [char]0x00F1 + [char]0x00E1)
    $Evidence = Join-Path $AppRoot 'build/ui-smoke/evidence-unicode'
}
New-Item -ItemType Directory -Force "$Fixture/app/config", $Evidence | Out-Null
Copy-Item "$AppRoot/TolkeyenAudioguia.exe" "$Fixture/app/TolkeyenAudioguia.exe" -Force
$Config = [IO.File]::ReadAllText("$AppRoot/config/audioguia.json") | ConvertFrom-Json
$Config.preferences.mode = 'simple'
$Config.preferences.direction = 'up'
$Config.preferences.simple_language = 'es'
$Config.preferences.custom_languages = @('pt', 'es')
$Config.preferences.volume = 0.01
$Config.preferences.gap_ms = 250
$Config.preferences.output_device = ''
$Config.preferences.compact_x = -1
$Config.preferences.compact_y = -1
$Config.preferences.always_on_top = $false

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Windows.Forms
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.IO;
public static class AudioguiaSmoke {
    public delegate bool Visitor(IntPtr handle, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, Visitor visitor, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(Visitor visitor, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr handle, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr handle, StringBuilder text, int length);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr handle, StringBuilder text, int length);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr handle, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr handle, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr handle);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr handle);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr handle);
    [DllImport("user32.dll")] public static extern short GetKeyState(int key);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr handle, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr handle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr handle, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool RedrawWindow(IntPtr handle, IntPtr rect, IntPtr region, uint flags);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr handle, ref Point point);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, UIntPtr size, uint allocation, uint protection);
    [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr process, IntPtr address, UIntPtr size, uint freeType);
    [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr written);
    [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] data, UIntPtr size, out UIntPtr read);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    // LVM_GETITEMRECT needs a RECT in the tested process, not a caller pointer.
    public static Rect ItemRect(IntPtr list, int index) {
        uint pid; GetWindowThreadProcessId(list,out pid);
        IntPtr process=OpenProcess(0x38,false,pid);
        if (process==IntPtr.Zero) throw new Exception("Cannot inspect test list geometry");
        IntPtr memory=IntPtr.Zero;
        try {
            memory=VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)16,0x3000,4);
            byte[] bytes=new byte[16]; UIntPtr size;
            if (memory==IntPtr.Zero || !WriteProcessMemory(process,memory,bytes,(UIntPtr)16,out size)) throw new Exception("Cannot prepare test RECT");
            if (SendMessageW(list,0x100E,(IntPtr)index,memory)==IntPtr.Zero || !ReadProcessMemory(process,memory,bytes,(UIntPtr)16,out size)) throw new Exception("Cannot read test row rectangle");
            Point origin=new Point(); ClientToScreen(list,ref origin);
            return new Rect { Left=origin.X+BitConverter.ToInt32(bytes,0), Top=origin.Y+BitConverter.ToInt32(bytes,4), Right=origin.X+BitConverter.ToInt32(bytes,8), Bottom=origin.Y+BitConverter.ToInt32(bytes,12) };
        } finally { if (memory!=IntPtr.Zero) VirtualFreeEx(process,memory,UIntPtr.Zero,0x8000); CloseHandle(process); }
    }
    public static string Text(IntPtr handle) {
        var text = new StringBuilder(32768); GetWindowTextW(handle,text,text.Capacity); return text.ToString();
    }
    public static string Class(IntPtr handle) {
        var text = new StringBuilder(256); GetClassNameW(handle,text,text.Capacity); return text.ToString();
    }
    public static IntPtr[] Children(IntPtr parent) {
        var children = new List<IntPtr>();
        EnumChildWindows(parent, (handle,data) => { children.Add(handle); return true; }, IntPtr.Zero);
        return children.ToArray();
    }
    public static IntPtr[] Windows(int process) {
        var windows = new List<IntPtr>();
        EnumWindows((handle,data) => { uint id; GetWindowThreadProcessId(handle,out id); if (id==process && IsWindowVisible(handle)) windows.Add(handle); return true; }, IntPtr.Zero);
        return windows.ToArray();
    }
    public static void Select(IntPtr combo, int index) {
        SendMessageW(combo,0x14E,(IntPtr)index,IntPtr.Zero);
        SendMessageW(GetParent(combo),0x111,(IntPtr)((GetDlgCtrlID(combo) & 0xFFFF) | (9 << 16)),combo);
    }
    public static void Key(IntPtr window, int key, bool extended, bool repeat) {
        long flags = 1 | (extended ? 1L<<24 : 0) | (repeat ? 1L<<30 : 0);
        PostMessageW(window,0x100,(IntPtr)key,(IntPtr)flags);
    }
    public static void MouseClick(IntPtr window) {
        PostMessageW(window,0x201,(IntPtr)1,(IntPtr)(8 | (8 << 16)));
        PostMessageW(window,0x202,IntPtr.Zero,(IntPtr)(8 | (8 << 16)));
    }
    public static void Wave(string path, double frequency) {
        using (var writer = new BinaryWriter(File.Create(path))) {
            const int frames = 96000;
            writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(36 + frames * 2);
            writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16);
            writer.Write((short)1); writer.Write((short)1); writer.Write(48000); writer.Write(96000);
            writer.Write((short)2); writer.Write((short)16);
            writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(frames * 2);
            for (int i=0;i<frames;i++) writer.Write((short)(3000 * Math.Sin(2*Math.PI*frequency*i/48000)));
        }
    }
}
'@
[void][AudioguiaSmoke]::SetProcessDPIAware()

foreach ($entry in @(@('es','0001-ESP',350),@('en','0002-ENG',440),@('pt','0003-PORT',550))) {
    New-Item -ItemType Directory -Force "$Fixture/audio/$($entry[1])" | Out-Null
    $relative = if ($NoAudio) { "audio/$($entry[1])/TEST_MISSING.wav" } else { "audio/$($entry[1])/TEST_ONLY.wav" }
    if (!$NoAudio) { [AudioguiaSmoke]::Wave("$Fixture/$relative", $entry[2]) }
    if ($Mp3Fixture) {
        $relative = "audio/$($entry[1])/TEST_ONLY.mp3"
        Copy-Item $Mp3Fixture "$Fixture/$relative" -Force
    }
    $Config.points[0].audio.($entry[0]) = $relative
}
[IO.File]::WriteAllText("$Fixture/app/config/audioguia.json", ($Config | ConvertTo-Json -Depth 12), (New-Object Text.UTF8Encoding($false)))
$Results = [Collections.Generic.List[string]]::new()
function Check($Condition, $Description) {
    if (!$Condition) { throw "FAIL: $Description" }
    $Results.Add("PASS: $Description")
    Write-Host "PASS: $Description"
}
function Find-Control([IntPtr]$Window, [string]$Text, [string]$Class = '') {
    $Root = [System.Windows.Automation.AutomationElement]::FromHandle($Window)
    foreach ($element in $Root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition)) {
        $handle = [IntPtr]$element.Current.NativeWindowHandle
        $Name = $element.Current.Name -replace '^(?:ENTER|\d+)\s*\|\s*',''
        $Name = ($Name -replace '\s+',' ').Trim()
        $Name = $Name -replace ' (REPRODUCIENDO|PAUSADO|VALIDANDO AUDIOS|PAUSA ENTRE IDIOMAS|ERROR)$',''
        if ($Name -eq $Text -and [AudioguiaSmoke]::IsWindowVisible($handle) -and (!$Class -or [AudioguiaSmoke]::Class($handle) -eq $Class)) { return $handle }
    }
    throw "Control not found: $Text ($Class)"
}
function Window-Texts([IntPtr]$Window) {
    $Root = [System.Windows.Automation.AutomationElement]::FromHandle($Window)
    return @($Root.FindAll([System.Windows.Automation.TreeScope]::Descendants,[System.Windows.Automation.Condition]::TrueCondition) | ForEach-Object { $_.Current.Name })
}
function Wait-Text([IntPtr]$Window, [string]$Text, [int]$Timeout = 6000) {
    $end = [DateTime]::UtcNow.AddMilliseconds($Timeout)
    do {
        if ((Window-Texts $Window) -contains $Text) { return $true }
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $end)
    return $false
}
function Get-Compact {
    $end = [DateTime]::UtcNow.AddSeconds(6)
    do {
        $handle = [AudioguiaSmoke]::Windows($Process.Id) | Where-Object { [AudioguiaSmoke]::Text($_) -eq 'Tolkeyen | Vista compacta' } | Select-Object -First 1
        if ($handle -and ![AudioguiaSmoke]::IsWindowVisible($Main)) { return [IntPtr]$handle }
        Start-Sleep -Milliseconds 80
    } while ([DateTime]::UtcNow -lt $end)
    throw 'Compact window did not become visible'
}
function Click([IntPtr]$Window, [string]$Text) {
    $handle = Find-Control $Window $Text 'Button'
    [void][AudioguiaSmoke]::SetForegroundWindow($Window)
    # Invoke the native button deterministically; dedicated checks below exercise
    # mouse down/up and focus. Posted clicks can race owner-drawn repaint at EOF.
    [void][AudioguiaSmoke]::PostMessageW($handle,0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 120
}
function Capture([IntPtr]$Window, [string]$Name) {
    [void][AudioguiaSmoke]::RedrawWindow($Window,[IntPtr]::Zero,[IntPtr]::Zero,0x185)
    Start-Sleep -Milliseconds 150
    $PreviousDpi = [AudioguiaSmoke]::SetThreadDpiAwarenessContext([IntPtr](-4))
    try {
        $rect = New-Object AudioguiaSmoke+Rect
        [void][AudioguiaSmoke]::GetWindowRect($Window,[ref]$rect)
        $bitmap = New-Object Drawing.Bitmap(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        $dc = $graphics.GetHdc()
        try { [void][AudioguiaSmoke]::PrintWindow($Window,$dc,2) }
        finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
        try { $bitmap.Save("$Evidence/$Name.png",[Drawing.Imaging.ImageFormat]::Png) }
        finally { $bitmap.Dispose() }
    } finally { [void][AudioguiaSmoke]::SetThreadDpiAwarenessContext($PreviousDpi) }
}

function Check-PointLayout([string]$Direction) {
    $Frame = New-Object AudioguiaSmoke+Rect
    [void][AudioguiaSmoke]::GetWindowRect($Main,[ref]$Frame)
    $ModeLabel = Find-Control $Main 'Modo' 'Static'
    $Footer = New-Object AudioguiaSmoke+Rect
    [void][AudioguiaSmoke]::GetWindowRect($ModeLabel,[ref]$Footer)
    $Rects = @()
    foreach ($point in $Config.points | Where-Object { $_.direction -eq $Direction }) {
        $Button = Find-Control $Main $point.name 'Button'
        $Rect = New-Object AudioguiaSmoke+Rect
        [void][AudioguiaSmoke]::GetWindowRect($Button,[ref]$Rect)
        Check ($Rect.Left -ge $Frame.Left -and $Rect.Right -le $Frame.Right -and $Rect.Bottom -le $Footer.Top -and ($Rect.Bottom-$Rect.Top) -ge 60) "Large $Direction point $($point.number) fits above the settings"
        foreach ($Previous in $Rects) {
            if ($Rect.Left -lt $Previous.Right -and $Rect.Right -gt $Previous.Left -and $Rect.Top -lt $Previous.Bottom -and $Rect.Bottom -gt $Previous.Top) {
                throw 'Point buttons overlap'
            }
        }
        $Rects += $Rect
    }
}

function Sequence-Rows([IntPtr]$Window) {
    $list = [AudioguiaSmoke]::Children($Window) | Where-Object { [AudioguiaSmoke]::Class($_) -eq 'SysListView32' } | Select-Object -First 1
    if (!$list) { throw 'Sequence list not found' }
    $count = [AudioguiaSmoke]::SendMessageW($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
    for ($i=0; $i -lt $count; $i++) { [AudioguiaSmoke]::ItemRect($list,$i) }
}
function Check-Sequence([string]$Expected, [string]$Description) {
    $deadline = [DateTime]::UtcNow.AddSeconds(3)
    do {
        $saved = [IO.File]::ReadAllText("$Fixture/app/config/audioguia.json") | ConvertFrom-Json
        if (($saved.preferences.custom_languages -join ',') -eq $Expected) { Check $true $Description; return }
        Start-Sleep -Milliseconds 60
    } while ([DateTime]::UtcNow -lt $deadline)
    Check $false $Description
}
function Sequence-Mouse([IntPtr]$Window, [int]$Source, [int]$Target = -1) {
    $rows = Sequence-Rows $Window
    Check ($rows.Count -eq 3) 'All three languages remain visible in the editor'
    $from = $rows[$Source]
    $cursor = New-Object AudioguiaSmoke+Point
    [void][AudioguiaSmoke]::GetCursorPos([ref]$cursor)
    [void][AudioguiaSmoke]::SetForegroundWindow($Window)
    try {
        $x = [int]($from.Left + $(if ($Target -lt 0) { 9 } else { 155 }))
        $y = [int](($from.Top + $from.Bottom) / 2)
        [void][AudioguiaSmoke]::SetCursorPos($x,$y)
        [AudioguiaSmoke]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
        if ($Target -ge 0) {
            Start-Sleep -Milliseconds 100
            [void][AudioguiaSmoke]::SetCursorPos(($x+10),$y)
            Start-Sleep -Milliseconds 150
            $to = $rows[$Target]
            [void][AudioguiaSmoke]::SetCursorPos($x,[int](($to.Top+$to.Bottom)/2))
            Start-Sleep -Milliseconds 200
        }
    } finally {
        [AudioguiaSmoke]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        [void][AudioguiaSmoke]::SetCursorPos($cursor.X,$cursor.Y)
    }
}

$Process = $null
try {
    # Different CWD and a system-only PATH check the portable's runtime closure.
    $OldPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot/System32;$env:SystemRoot"
        $Process = Start-Process "$Fixture/app/TolkeyenAudioguia.exe" -WorkingDirectory $env:SystemRoot -PassThru
    } finally { $env:PATH = $OldPath }
    Start-Sleep -Milliseconds 1500
    $Process.Refresh()
    $Main = $Process.MainWindowHandle
    Check ($Main -ne [IntPtr]::Zero) 'Main window opens from a different working directory without development PATH'
    Capture $Main 'main-startup'
    Check (Wait-Text $Main 'DETENIDO') 'Startup is stopped without autoplay'
    Check ((Find-Control $Main 'Bienvenida' 'Button') -ne [IntPtr]::Zero) 'UP buttons exist'
    Check-PointLayout 'up'
    Click $Main 'DOWN | 6 puntos'
    Check ((Find-Control $Main 'Saint Christopher' 'Button') -ne [IntPtr]::Zero) 'DOWN buttons exist after switching group'
    Check-PointLayout 'down'
    $HiddenUp = [AudioguiaSmoke]::Children($Main) | Where-Object { [AudioguiaSmoke]::Text($_) -eq 'Bienvenida' -and [AudioguiaSmoke]::IsWindowVisible($_) }
    Check (!$HiddenUp) 'Only the selected group is visible'
    Capture $Main 'main-down'
    Click $Main 'UP | 8 puntos'
    $Original = New-Object AudioguiaSmoke+Rect
    [void][AudioguiaSmoke]::GetWindowRect($Main,[ref]$Original)
    [void][AudioguiaSmoke]::MoveWindow($Main,$Original.Left,$Original.Top,1024,740,$true)
    Start-Sleep -Milliseconds 300
    Check-PointLayout 'up'
    Capture $Main 'main-1024'
    [void][AudioguiaSmoke]::MoveWindow($Main,$Original.Left,$Original.Top,($Original.Right-$Original.Left),($Original.Bottom-$Original.Top),$true)
    Start-Sleep -Milliseconds 300
    Capture $Main 'main'
    Click $Main 'Vista compacta'
    $Compact = Get-Compact
    Check ($Compact -ne $Main -and [AudioguiaSmoke]::IsWindowVisible($Compact)) 'Compact view replaces the full view'
    Check (Wait-Text $Compact 'DETENIDO') 'Compact view shares stopped state'
    Capture $Compact 'compact'
    Click $Compact 'Vista completa'
    Check ([AudioguiaSmoke]::IsWindowVisible($Main)) 'Return to full view'

    Click $Main 'Configurar audios y salida'
    $Deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $Dialog = [AudioguiaSmoke]::Windows($Process.Id) | Where-Object { [AudioguiaSmoke]::Text($_) -like 'Configurar*Tolkeyen' } | Select-Object -First 1
    } while (!$Dialog -and [DateTime]::UtcNow -lt $Deadline)
    Check ($null -ne $Dialog -and $Dialog -ne [IntPtr]::Zero) 'Preferences dialog opens'
    Capture $Dialog 'preferences'
    Click $Dialog 'Cancelar'
    Click $Main 'Bienvenida'
    if ($NoAudio) {
        Check (Wait-Text $Main 'ERROR') 'Missing files produce an explicit error'
        Capture $Main 'missing-audio'
    } else {
        $Format = if ($Mp3Fixture) { 'MP3' } else { 'WAV' }
        Check (Wait-Text $Main 'REPRODUCIENDO') "Real WASAPI starts the generated TEST_ONLY $Format"
        Click $Main 'Pausar'
        Check (Wait-Text $Main 'PAUSADO') 'Pause updates the shared player'
        Click $Main 'Vista compacta'
        $Compact = Get-Compact
        Check (Wait-Text $Compact 'PAUSADO') 'Compact view shares the paused point'
        Capture $Compact 'compact-paused'
        Click $Compact 'Reanudar'
        Check (Wait-Text $Compact 'REPRODUCIENDO') 'Resume from compact view'
        Click $Compact 'Detener'
        Check (Wait-Text $Compact 'DETENIDO') 'Stop clears the sequence from compact view'
        Click $Compact 'Vista completa'
        $Mode = Find-Control $Main 'Simple' 'ComboBox'
        [AudioguiaSmoke]::Select($Mode,1)
        Click $Main 'Bienvenida'
        Check (Wait-Text $Main 'PAUSA ENTRE IDIOMAS') 'Sequential mode inserts its configured gap'
        Check (Wait-Text $Main 'DETENIDO' 12000) 'Sequential mode finishes the same point without advancing to another'
        [AudioguiaSmoke]::Select($Mode,2)
        Click $Main 'Bienvenida'
        Check (Wait-Text $Main 'REPRODUCIENDO') 'Custom mode starts'
        $Texts = (Window-Texts $Main) -join "`n"
        Check ($Texts -match 'PORT > ESP') 'Custom subset and order appear in the pending selection'
        Click $Main 'Detener'
        Click $Main 'Editar idiomas...'
        Start-Sleep -Milliseconds 250
        $SequenceDialog = [AudioguiaSmoke]::Windows($Process.Id) | Where-Object { [AudioguiaSmoke]::Text($_) -eq 'Preparar idiomas personalizados' } | Select-Object -First 1
        Check ($null -ne $SequenceDialog) 'Custom sequence editor opens separately from the point grid'
        Capture $SequenceDialog 'sequence-editor'
        Click $SequenceDialog 'Bajar'
        Check-Sequence 'es,pt' 'Bajar moves the selected language and persists its new position'
        Click $SequenceDialog 'Subir'
        Check-Sequence 'pt,es' 'Subir preserves selection and restores the original order'
        Sequence-Mouse $SequenceDialog 0 1
        Check-Sequence 'es,pt' 'Dragging downward reorders active languages'
        Sequence-Mouse $SequenceDialog 2
        Check-Sequence 'es,pt,en' 'Checking an inactive language appends it to the sequence'
        Capture $SequenceDialog 'sequence-three-languages'
        Sequence-Mouse $SequenceDialog 2 0
        Check-Sequence 'en,es,pt' 'Dragging last to first shifts all intervening languages'
        Sequence-Mouse $SequenceDialog 0
        Check-Sequence 'es,pt' 'Unchecking a language excludes it from playback'
        Sequence-Mouse $SequenceDialog 1
        Check-Sequence 'es' 'A single selected language is supported'
        Sequence-Mouse $SequenceDialog 0
        Check-Sequence 'es' 'The final active language cannot be unchecked'
        Check (Wait-Text $SequenceDialog 'Debe quedar al menos un idioma. Active otro antes de quitar el ultimo.') 'Last-language rejection explains how to continue'
        Sequence-Mouse $SequenceDialog 2
        Check-Sequence 'es,pt' 'An excluded language can be enabled again'
        Sequence-Mouse $SequenceDialog 1 0
        Check-Sequence 'pt,es' 'Dragging upward restores the custom subset'
        Click $SequenceDialog 'Listo'
        Start-Sleep -Milliseconds 200
        Click $Main 'DOWN | 6 puntos'
        Click $Main 'Monte Cervantes'
        Check (Wait-Text $Main 'ERROR') 'Another point cancels and reports its missing files'
        Capture $Main 'missing-audio'
        Click $Main 'Detener'
        Click $Main 'UP | 8 puntos'
        [AudioguiaSmoke]::Select($Mode,0)
        $Point = Find-Control $Main 'Bienvenida' 'Button'
        [void][AudioguiaSmoke]::SetForegroundWindow($Main)
        Start-Sleep -Milliseconds 150
        Check ([AudioguiaSmoke]::GetForegroundWindow() -eq $Main) 'Full view owns focus before keyboard checks'
        [AudioguiaSmoke]::MouseClick($Point)
        Check (Wait-Text $Main 'REPRODUCIENDO') 'Native mouse click focuses a point button'
        Click $Main 'Detener'
        [AudioguiaSmoke]::Key($Point,0x0D,$true,$false)
        Check (Wait-Text $Main 'DETENIDO') 'Numpad Enter while stopped never starts audio'
        [AudioguiaSmoke]::MouseClick($Point)
        Check (Wait-Text $Main 'REPRODUCIENDO') 'Playback for Enter checks independent of Num Lock'
        [AudioguiaSmoke]::Key($Point,0x0D,$true,$false)
        Check (Wait-Text $Main 'PAUSADO') 'Numpad Enter pauses with either Num Lock state'
        Capture $Main 'main-paused'
        [AudioguiaSmoke]::Key($Point,0x0D,$true,$true)
        Start-Sleep -Milliseconds 150
        Check (Wait-Text $Main 'PAUSADO') 'Repeated Enter is ignored with either Num Lock state'
        [AudioguiaSmoke]::Key($Point,0x0D,$true,$false)
        Check (Wait-Text $Main 'REPRODUCIENDO') 'Numpad Enter resumes with either Num Lock state'
        Click $Main 'Detener'
        [AudioguiaSmoke]::MouseClick($Mode)
        Start-Sleep -Milliseconds 100
        [AudioguiaSmoke]::Key($Mode,0x1B,$false,$false)
        Check (Wait-Text $Main 'ATAJOS SUSPENDIDOS | Termine de editar y pulse Usar numpad') 'Editing suspension is visible with either Num Lock state'
        Click $Main 'Usar numpad'
        $ReadyText = if (([AudioguiaSmoke]::GetKeyState(0x90) -band 1) -ne 0) { 'NUMPAD LISTO | El numero grande de cada boton es su tecla' } else { 'BLOQ NUM APAGADO | Activelo para usar las teclas numericas' }
        Check (Wait-Text $Main $ReadyText) 'Ready button leaves editing and shows the actual Num Lock state'
        Check (Wait-Text $Main 'DETENIDO') 'Ready button does not start audio'
        if (([AudioguiaSmoke]::GetKeyState(0x90) -band 1) -ne 0) {
            [AudioguiaSmoke]::Key($Point,0x61,$false,$false)
            Check (Wait-Text $Main 'REPRODUCIENDO') 'Focused numpad 1 starts the active-direction point'
            [AudioguiaSmoke]::Key($Point,0x0D,$true,$false)
            Check (Wait-Text $Main 'PAUSADO') 'Extended numpad Enter pauses'
            [AudioguiaSmoke]::Key($Point,0x0D,$true,$true)
            Start-Sleep -Milliseconds 250
            Check ((Window-Texts $Main) -contains 'PAUSADO') 'Held Enter repeat does not resume'
            [AudioguiaSmoke]::Key($Point,0x0D,$true,$false)
            Check (Wait-Text $Main 'REPRODUCIENDO') 'Next numpad Enter resumes'
            [AudioguiaSmoke]::Key($Point,0x60,$false,$false)
            Check (Wait-Text $Main 'DETENIDO') 'Numpad 0 stops and clears playback'
            [AudioguiaSmoke]::MouseClick($Mode)
            Start-Sleep -Milliseconds 150
            [AudioguiaSmoke]::Key($Mode,0x1B,$false,$false)
            [AudioguiaSmoke]::Key($Mode,0x62,$false,$false)
            Start-Sleep -Milliseconds 250
            Check ((Window-Texts $Main) -contains 'DETENIDO') 'Numpad does not fire while editing a selector'
            Check (Wait-Text $Main 'ATAJOS SUSPENDIDOS | Termine de editar y pulse Usar numpad') 'Editing suspension is visible to the operator'
            Click $Main 'Usar numpad'
            Check (Wait-Text $Main 'NUMPAD LISTO | El numero grande de cada boton es su tecla') 'Ready button restores numpad without starting playback'
            Check (Wait-Text $Main 'DETENIDO') 'Returning to numpad does not trigger audio'
            [AudioguiaSmoke]::MouseClick($Point)
            Check (Wait-Text $Main 'REPRODUCIENDO') 'Mouse returns focus from the selector to the point'
            Click $Main 'Detener'
            [AudioguiaSmoke]::Key($Point,0x61,$false,$false)
            Check (Wait-Text $Main 'REPRODUCIENDO') 'A second explicit numpad press starts a new sequence'
            for ($i=0; $i -lt 4; $i++) {
                Start-Sleep -Milliseconds 300
                [AudioguiaSmoke]::Key($Point,0x61,$false,$true)
            }
            Check (Wait-Text $Main 'DETENIDO' 1300) 'Held digit repeat does not restart the track'
            Click $Main 'Vista compacta'
            $Compact = Get-Compact
            $CompactStop = Find-Control $Compact 'Detener' 'Button'
            [void][AudioguiaSmoke]::SetForegroundWindow($Compact)
            [AudioguiaSmoke]::MouseClick($CompactStop)
            Start-Sleep -Milliseconds 150
            [AudioguiaSmoke]::Key($CompactStop,0x61,$false,$false)
            Check (Wait-Text $Compact 'REPRODUCIENDO') 'Focused compact view accepts numpad points'
            [AudioguiaSmoke]::Key($CompactStop,0x0D,$true,$false)
            Check (Wait-Text $Compact 'PAUSADO') 'Numpad Enter pauses from compact view'
            [AudioguiaSmoke]::Key($CompactStop,0x60,$false,$false)
            Check (Wait-Text $Compact 'DETENIDO') 'Numpad 0 stops from compact view'
            Click $Compact 'Vista completa'
        } else { $Results.Add('SKIP: numeric numpad smoke requires Num Lock already on; test did not change it') }
        Click $Main 'Bienvenida'
        Check (Wait-Text $Main 'REPRODUCIENDO') 'Playback for focus-isolation test'
        $Other = New-Object Windows.Forms.Form
        try {
            $Other.Text = 'Audioguia focus test (separate process window)'
            $Other.TopMost = $true
            $Other.Show()
            $Other.Activate()
            [Windows.Forms.Application]::DoEvents()
            # A normal test click activates this owned form without changing OS focus policy.
            $Cursor = New-Object AudioguiaSmoke+Point
            $Rect = New-Object AudioguiaSmoke+Rect
            [void][AudioguiaSmoke]::GetCursorPos([ref]$Cursor)
            [void][AudioguiaSmoke]::GetWindowRect($Other.Handle,[ref]$Rect)
            [void][AudioguiaSmoke]::SetCursorPos(($Rect.Left+35),($Rect.Top+45))
            [AudioguiaSmoke]::mouse_event(2,0,0,0,[UIntPtr]::Zero)
            [AudioguiaSmoke]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
            [Windows.Forms.Application]::DoEvents()
            [void][AudioguiaSmoke]::SetCursorPos($Cursor.X,$Cursor.Y)
            Start-Sleep -Milliseconds 150
            Check ([AudioguiaSmoke]::GetForegroundWindow() -eq $Other.Handle) 'An external window owns the foreground'
            [AudioguiaSmoke]::Key($Point,0x62,$false,$false)
            Start-Sleep -Milliseconds 200
            Check ((Window-Texts $Main) -contains 'REPRODUCIENDO') 'A numpad message cannot trigger another point without app focus'
            Check (Wait-Text $Main 'DETENIDO') 'Playback finishes while another window has focus'
            Check ([AudioguiaSmoke]::GetForegroundWindow() -eq $Other.Handle) 'Playback updates do not steal focus'
        } finally { $Other.Close(); $Other.Dispose() }
    }
    $Results.Add('Executable SHA256: ' + (Get-FileHash "$Fixture/app/TolkeyenAudioguia.exe" -Algorithm SHA256).Hash)
} catch {
    $Results.Add('FAIL: ' + $_.Exception.Message)
    if ($Process -and !$Process.HasExited -and $Main -ne [IntPtr]::Zero) {
        Capture $Main 'failure'
        [IO.File]::WriteAllLines("$Evidence/failure-state.txt", (Window-Texts $Main), (New-Object Text.UTF8Encoding($false)))
    }
    throw
} finally {
    if ($Process -and !$Process.HasExited) {
        foreach ($window in [AudioguiaSmoke]::Windows($Process.Id)) {
            if ($window -ne $Main) { [void][AudioguiaSmoke]::PostMessageW($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero) }
        }
        Start-Sleep -Milliseconds 150
        if ($Main -ne [IntPtr]::Zero) {
            [void][AudioguiaSmoke]::PostMessageW($Main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        } else { [void]$Process.CloseMainWindow() }
        if (!$Process.WaitForExit(5000)) { $Process.Kill(); [void]$Process.WaitForExit(5000) }
    }
    [IO.File]::WriteAllLines("$Evidence/results.txt", $Results, (New-Object Text.UTF8Encoding($false)))
}
