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
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
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

foreach ($entry in @(@('es','0001-ESP',350),@('en','0002-ENG',440),@('pt','0003-PORR',550))) {
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
        if ($element.Current.Name -eq $Text -and (!$Class -or [AudioguiaSmoke]::Class($handle) -eq $Class)) { return $handle }
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
    [AudioguiaSmoke]::MouseClick($handle)
    Start-Sleep -Milliseconds 120
}
function Capture([IntPtr]$Window, [string]$Name) {
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
    Check ((Window-Texts $Main) -contains 'Bienvenida') 'UP buttons exist'
    Check ((Window-Texts $Main) -contains 'Saint Christopher') 'DOWN buttons exist'
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
        Click $Main 'Monte Cervantes'
        Check (Wait-Text $Main 'ERROR') 'Another point cancels and reports its missing files'
        Capture $Main 'missing-audio'
        Click $Main 'Detener'
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
        if ($Main -ne [IntPtr]::Zero) {
            [void][AudioguiaSmoke]::PostMessageW($Main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
        } else { [void]$Process.CloseMainWindow() }
        [void]$Process.WaitForExit(5000)
    }
    [IO.File]::WriteAllLines("$Evidence/results.txt", $Results, (New-Object Text.UTF8Encoding($false)))
}
