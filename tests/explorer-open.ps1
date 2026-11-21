# Run from an interactive Windows session with no competing Explorer tab bar.
param([switch]$MeasureOnly)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class OpenTest {
    [DllImport("user32")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32")] public static extern bool IsIconic(IntPtr w);
    [DllImport("user32")] public static extern bool IsWindow(IntPtr w);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
    [StructLayout(LayoutKind.Sequential)] struct GuiInfo {
        public int size,flags; public IntPtr active,focus,capture,menu,move,caret; public int x,y,right,bottom;
    }
    [DllImport("user32")] static extern bool GetGUIThreadInfo(uint tid,ref GuiInfo info);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,System.Text.StringBuilder name,int size);
    public static string FocusClass() {
        var info=new GuiInfo(); info.size=Marshal.SizeOf(info);
        var name=new System.Text.StringBuilder(128);
        if (GetGUIThreadInfo(0,ref info)) GetClassName(info.focus,name,name.Capacity);
        return name.ToString();
    }
    [DllImport("user32")] public static extern bool ShowWindowAsync(IntPtr w,int mode);
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
    [DllImport("user32")] static extern IntPtr SendMessageTimeout(IntPtr w,int m,IntPtr a,IntPtr b,int flags,int ms,out IntPtr value);
    [DllImport("dwmapi")] public static extern int DwmFlush();
    public struct Point { public int X,Y; }
    [DllImport("user32")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32")] static extern bool ClientToScreen(IntPtr w,ref Point p);
    [DllImport("user32")] static extern bool GetCursorPos(out Point p);
    [DllImport("user32")] static extern bool SetCursorPos(int x,int y);
    [DllImport("user32")] static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr info);
    [DllImport("user32")] static extern void keybd_event(byte vk,byte scan,uint flags,UIntPtr info);
    public static void Ctrl(IntPtr root,char letter,bool shift=false) {
        if (GetForegroundWindow()!=root) throw new Exception("Explorer lost focus before keyboard input.");
        keybd_event(0x11,0,0,UIntPtr.Zero);
        if (shift) keybd_event(0x10,0,0,UIntPtr.Zero);
        keybd_event((byte)letter,0,0,UIntPtr.Zero);
        keybd_event((byte)letter,0,2,UIntPtr.Zero);
        if (shift) keybd_event(0x10,0,2,UIntPtr.Zero);
        keybd_event(0x11,0,2,UIntPtr.Zero);
    }
    public static void ClickLauncher(IntPtr w) {
        Point p = new Point { X=20,Y=20 }, before;
        if (!ClientToScreen(w,ref p) || WindowFromPoint(p)!=w || !GetCursorPos(out before)) throw new Exception("Test launcher is covered.");
        SetCursorPos(p.X,p.Y);
        try { if (WindowFromPoint(p)==w) mouse_event(6,0,0,0,UIntPtr.Zero); }
        finally { SetCursorPos(before.X,before.Y); }
    }
    public static int Count(IntPtr bar) {
        IntPtr value;
        if (SendMessageTimeout(GetDlgItem(bar,102),0x1304,IntPtr.Zero,IntPtr.Zero,2,1000,out value)==IntPtr.Zero)
            throw new Exception("Explorer tab control disappeared or stopped responding.");
        return value.ToInt32();
    }
}
'@
function Wait-Open($label,[scriptblock]$probe) {
    $end = [DateTime]::UtcNow.AddSeconds(12)
    do {
        if (& $probe) { return }
        [Windows.Forms.Application]::DoEvents()
        [Threading.Thread]::Sleep(5)
    } while ([DateTime]::UtcNow -lt $end)
    throw "Timed out waiting for $label (focused control $([OpenTest]::FocusClass()), tabs $([OpenTest]::Count($bar)))"
}
$shell = New-Object -ComObject Shell.Application
foreach ($web in $shell.Windows()) {
    if ([OpenTest]::IsWindowVisible([FixtureWindow]::GetProp([IntPtr][long]$web.HWND,'EZFolder.Band.2'))) {
        throw 'Another Explorer band could receive the test open.'
    }
}
$dir = Join-Path ([IO.Path]::GetTempPath()) ('EZFolder-open-'+[guid]::NewGuid().ToString('N'))
$window = $launcher = $null
$paths = @(1..3 | ForEach-Object { Join-Path $dir "Open-$_" })
try {
    foreach ($p in $paths) { [void][IO.Directory]::CreateDirectory((Join-Path $p ('Item-'+[IO.Path]::GetFileName($p)))) }
    $window = New-TestExplorer $dir -Separate
    $root = [IntPtr][long]$window.HWND
    Wait-Open 'test band' { [FixtureWindow]::GetProp($root,'EZFolder.Band.2') -ne [IntPtr]::Zero }
    $bar = [FixtureWindow]::GetProp($root,'EZFolder.Band.2')
    $element = [Windows.Automation.AutomationElement]::FromHandle($root)
    $launcher = New-Object Windows.Forms.Form
    $launcher.Text = 'EZFolder open test'
    $launcher.ShowInTaskbar = $false
    $launcher.Show()
    foreach ($p in $paths) {
        $launcher.Activate()
        [void][OpenTest]::SetForegroundWindow($launcher.Handle)
        if ([OpenTest]::GetForegroundWindow() -ne $launcher.Handle) {
            $launcher.TopMost=$true
            try { [OpenTest]::ClickLauncher($launcher.Handle) } finally { $launcher.TopMost=$false }
        }
        Wait-Open 'foreground test launcher' { [OpenTest]::GetForegroundWindow() -eq $launcher.Handle }
        if ($p -eq $paths[-1]) {
            [void][OpenTest]::ShowWindowAsync($root,6)
            Wait-Open 'minimized receiver' { [OpenTest]::IsIconic($root) }
        }
        $name = 'Item-'+[IO.Path]::GetFileName($p)
        $byName = New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty,$name)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        $shell.Explore($p)
        Wait-Open 'target shell view' { $window.Document.Folder.Self.Path -eq $p }
        $navigation = $watch.ElapsedMilliseconds
        if (!$MeasureOnly) {
            Wait-Open 'non-minimized foreground receiver' { [OpenTest]::GetForegroundWindow() -eq $root -and ![OpenTest]::IsIconic($root) }
            Wait-Open 'visible folder item' {
                $item = $element.FindFirst([Windows.Automation.TreeScope]::Descendants,$byName)
                $item -and !$item.Current.IsOffscreen
            }
            [void][OpenTest]::DwmFlush()
        }
        "Open $name : shell view $navigation ms, observed $($watch.ElapsedMilliseconds) ms, foreground=$([OpenTest]::GetForegroundWindow() -eq $root)"
    }
    if ($MeasureOnly) { return }
    $launcher.Close()
    $before = [OpenTest]::Count($bar)
    $item = $element.FindFirst([Windows.Automation.TreeScope]::Descendants,$byName)
    $item.SetFocus()
    [OpenTest]::Ctrl($root,'W')
    Wait-Open 'Ctrl+W from file view closes exactly one tab' { [OpenTest]::Count($bar) -eq $before-1 }
    'PASS: Ctrl+W from the folder view closed one tab.'
    while ([OpenTest]::Count($bar) -gt 1) {
        $before = [OpenTest]::Count($bar)
        [OpenTest]::Ctrl($root,'L')
        Wait-Open 'address edit focus' { [OpenTest]::FocusClass() -eq 'Edit' }
        [OpenTest]::Ctrl($root,'W')
        Wait-Open 'Ctrl+W from address bar closes one tab' { [OpenTest]::Count($bar) -eq $before-1 }
    }
    $lastFolder=$window.Document.Folder.Self.Path
    [OpenTest]::Ctrl($root,'W')
    Wait-Open 'last tab returns to the default location in the original frame' {
        [OpenTest]::IsWindow($root) -and [OpenTest]::Count($bar) -eq 0 -and
            $window.Document.Folder.Self.Path -match '20D04FE0|679f85cb'
    }
    [OpenTest]::Ctrl($root,'W')
    Start-Sleep -Milliseconds 150
    [OpenTest]::Ctrl($root,'T',$true)
    Wait-Open 'restore the last closed folder as the only tab' { [OpenTest]::Count($bar) -eq 1 -and $window.Document.Folder.Self.Path -eq $lastFolder }
    [OpenTest]::Ctrl($root,'W')
    Wait-Open 'receiver is empty again' { [OpenTest]::Count($bar) -eq 0 }
    $shell.Explore($paths[0])
    Wait-Open 'external open takes the first slot in the same frame' { [OpenTest]::Count($bar) -eq 1 -and $window.Document.Folder.Self.Path -eq $paths[0] }
    'PASS: receiver activated from another foreground window and restored from minimized. Ctrl+W preserved the frame.'
} finally {
    if ($launcher) { $launcher.Dispose() }
    if ($window -and [OpenTest]::IsWindow($root)) { $window.Quit() }
    foreach ($web in $shell.Windows()) { if ($web.Document.Folder.Self.Path -in $paths) { $web.Quit() } }
    foreach ($p in $paths) {
        $marker = Join-Path $p ('Item-'+[IO.Path]::GetFileName($p))
        if ([IO.Directory]::Exists($marker)) { [IO.Directory]::Delete($marker) }
        if ([IO.Directory]::Exists($p)) { [IO.Directory]::Delete($p) }
    }
    if ([IO.Directory]::Exists($dir)) { [IO.Directory]::Delete($dir) }
}
