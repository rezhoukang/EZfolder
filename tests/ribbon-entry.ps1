param([string]$PreviewPath,[switch]$TestHover,[switch]$TestSettings)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class RibbonEntryTest {
    public struct Rect { public int Left, Top, Right, Bottom; }
    public struct Point { public int X,Y; public Point(int x,int y){X=x;Y=y;} }
    delegate bool Visit(IntPtr w,IntPtr p);
    [DllImport("user32")] static extern bool EnumChildWindows(IntPtr w, Visit v,IntPtr p);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder s,int n);
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr w,int id);
    [DllImport("user32")] public static extern IntPtr GetParent(IntPtr w);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern IntPtr GetProp(IntPtr w,string k);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32")] public static extern IntPtr WindowFromPoint(Point p);
    [DllImport("user32")] public static extern bool GetCursorPos(out Point p);
    [DllImport("user32")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint process);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string name);
    [DllImport("user32",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")]
    static extern IntPtr ReadText(IntPtr w,int m,IntPtr size,StringBuilder text,int flags,int ms,out IntPtr result);
    public static IntPtr Tip(IntPtr button) {
        uint owner,pid;
        GetWindowThreadProcessId(button,out owner);
        IntPtr tip=IntPtr.Zero;
        while((tip=FindWindowEx(IntPtr.Zero,tip,"tooltips_class32",null))!=IntPtr.Zero){
            GetWindowThreadProcessId(tip,out pid);
            if(pid!=owner || !IsWindowVisible(tip)) continue;
            var text=new StringBuilder(128); IntPtr result;
            if(ReadText(tip,13,(IntPtr)text.Capacity,text,2,300,out result)!=IntPtr.Zero && text.ToString()=="EZfolder") return tip;
        }
        return IntPtr.Zero;
    }
    [DllImport("user32")] public static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32")] static extern IntPtr SendMessageTimeout(IntPtr w,int m,IntPtr a,IntPtr b,int flags,int ms,out IntPtr result);
    [DllImport("user32")] static extern bool PostMessage(IntPtr w,int m,IntPtr a,IntPtr b);
    [DllImport("user32")] static extern uint GetMenuItemID(IntPtr menu,int pos);
    public static IntPtr SettingsPopup(IntPtr button) {
        uint owner,pid;
        GetWindowThreadProcessId(button,out owner);
        for (IntPtr w=FindWindowEx(IntPtr.Zero,IntPtr.Zero,"#32768",null); w!=IntPtr.Zero; w=FindWindowEx(IntPtr.Zero,w,"#32768",null)) {
            GetWindowThreadProcessId(w,out pid);
            IntPtr menu;
            if (pid!=owner || !IsWindowVisible(w) || SendMessageTimeout(w,0x1E1,IntPtr.Zero,IntPtr.Zero,2,300,out menu)==IntPtr.Zero) continue;
            if (GetMenuItemID(menu,0)==200) return w;
        }
        return IntPtr.Zero;
    }
    public static void Context(IntPtr button) {
        var r=Bounds(button);
        var pt=new Point((r.Left+r.Right)/2,(r.Top+r.Bottom)/2);
        if (WindowFromPoint(pt)!=button) throw new Exception("The settings entry is covered.");
        SetCursorPos(pt.X,pt.Y);
        mouse_event(8,0,0,0,UIntPtr.Zero);
        mouse_event(16,0,0,0,UIntPtr.Zero);
    }
    public static void Key(IntPtr popup,int vk) { PostMessage(popup,0x100,(IntPtr)vk,IntPtr.Zero); }
    public static void Stroke(IntPtr w,int vk) {
        PostMessage(w,0x100,(IntPtr)vk,(IntPtr)1);
        PostMessage(w,0x101,(IntPtr)vk,(IntPtr)unchecked((int)0xc0000001));
    }
    public static int TabCount(IntPtr tabs) {
        IntPtr result;
        if(SendMessageTimeout(tabs,0x1304,IntPtr.Zero,IntPtr.Zero,2,500,out result)==IntPtr.Zero) throw new Exception("Tabs unavailable");
        return result.ToInt32();
    }
    public static void DeliverKey(IntPtr tabs,int vk) {
        IntPtr result;
        if (SendMessageTimeout(tabs,0x100,(IntPtr)vk,(IntPtr)1,2,500,out result)==IntPtr.Zero) throw new Exception("Tab key delivery failed.");
    }
    public static void FocusTabs(IntPtr root,IntPtr tabs) {
        SetForegroundWindow(root);
        IntPtr result;
        SendMessageTimeout(tabs,0x201,(IntPtr)1,(IntPtr)0x8000c,2,1000,out result);
        SendMessageTimeout(tabs,0x202,IntPtr.Zero,(IntPtr)0x8000c,2,1000,out result);
    }
    [StructLayout(LayoutKind.Sequential)]
    struct GuiInfo { public int size,flags; public IntPtr active,focus,capture,menu,move,caret; public Rect rect; }
    [DllImport("user32")] static extern bool GetGUIThreadInfo(uint thread,ref GuiInfo info);
    public static bool Focused(IntPtr w) { var g=new GuiInfo();g.size=Marshal.SizeOf(g);return GetGUIThreadInfo(0,ref g)&&g.focus==w; }
    public static IntPtr SettingsDialog(IntPtr button) {
        uint owner,pid;
        GetWindowThreadProcessId(button,out owner);
        string expected=System.Diagnostics.Process.GetProcessById((int)owner).ProcessName.Replace("EZFolderEntry","EZFolderSettings");
        for (IntPtr w=FindWindowEx(IntPtr.Zero,IntPtr.Zero,null,"EZFolder 设置");w!=IntPtr.Zero;w=FindWindowEx(IntPtr.Zero,w,null,"EZFolder 设置")) {
            GetWindowThreadProcessId(w,out pid);
            if(IsWindowVisible(w) && System.Diagnostics.Process.GetProcessById((int)pid).ProcessName==expected) return w;
        }
        return IntPtr.Zero;
    }
    public static IntPtr XamlRoot(IntPtr w) {
        return FindWindowEx(w,IntPtr.Zero,"Windows.UI.Composition.DesktopWindowContentBridge",null);
    }
    [DllImport("user32")] static extern bool GetMenuItemRect(IntPtr w,IntPtr menu,int pos,out Rect rect);
    [DllImport("user32")] static extern void mouse_event(uint flags,int dx,int dy,uint data,UIntPtr extra);
    public static void Pick(IntPtr popup,int pos) {
        IntPtr menu; Rect rect;
        if (SendMessageTimeout(popup,0x1E1,IntPtr.Zero,IntPtr.Zero,2,300,out menu)==IntPtr.Zero ||
            !GetMenuItemRect(IntPtr.Zero,menu,pos,out rect)) throw new Exception("Cannot locate choice");
        Point pt=new Point((rect.Left+rect.Right)/2,(rect.Top+rect.Bottom)/2);
        if (WindowFromPoint(pt)!=popup) throw new Exception("Choice is covered");
        SetCursorPos(pt.X,pt.Y);
        System.Threading.Thread.Sleep(80);
        if (WindowFromPoint(pt)!=popup) throw new Exception("Menu moved before selection");
        mouse_event(2,0,0,0,UIntPtr.Zero);
        mouse_event(4,0,0,0,UIntPtr.Zero);
    }
    public static IntPtr Entry(IntPtr root) {
        IntPtr found=IntPtr.Zero;
        EnumChildWindows(root,(w,p)=>{var s=new StringBuilder(64);GetClassName(w,s,64);
            if(s.ToString()!="EZFolder.RibbonEntry"||!IsWindowVisible(w))return true;
            found=w;return false;},IntPtr.Zero);
        return found;
    }
    public static bool Shown(IntPtr root){return IsWindowVisible(GetProp(root,"EZFolder.Band.2"));}
    public static Rect Bounds(IntPtr w){Rect r;GetWindowRect(w,out r);return r;}
    public static void Click(IntPtr button){
        IntPtr result;
        if(SendMessageTimeout(button,0xF5,IntPtr.Zero,IntPtr.Zero,2,3000,out result)==IntPtr.Zero)
            throw new Exception("Entry button did not respond.");
    }
}
'@
function Wait-For($description,[scriptblock]$predicate) {
    Write-Verbose $description
    for ($i=0; $i -lt 80; $i++) {
        $result = & $predicate
        if ($result) { return $result }
        Start-Sleep -Milliseconds 80
    }
    throw "Timed out: $description"
}
function Test-Placement {
    $r = [RibbonEntryTest]::Bounds($button)
    $dock = [RibbonEntryTest]::Bounds([RibbonEntryTest]::GetParent($entry))
    $margin = [math]::Round(42 * [RibbonEntryTest]::GetDpiForWindow($entry) / 96)
    $point = [RibbonEntryTest+Point]::new(($r.Left+$r.Right)/2,($r.Top+$r.Bottom)/2)
    return ([math]::Abs($dock.Right-$r.Right-$margin) -le 1 -and $r.Top -eq $dock.Top -and
        [RibbonEntryTest]::WindowFromPoint($point) -eq $button)
}
function Test-SettingsMenu {
    Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
    function SettingControl($name) {
        $root = [Windows.Automation.AutomationElement]::FromHandle([RibbonEntryTest]::XamlRoot($dialog))
        $id = [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$name)
        $control = $root.FindFirst([Windows.Automation.TreeScope]::Descendants,$id)
        if (!$control -or $control.Current.FrameworkId -ne 'XAML') { throw "Missing Windows XAML control: $name" }
        return $control
    }
    $settings = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\EZFolder')
    $prior = $settings.GetValue('NewTabLocation')
    $priorPreferences = $settings.GetValue('PreferencesV1')
    $popup = [IntPtr]::Zero
    $saved = New-Object RibbonEntryTest+Point
    if (![RibbonEntryTest]::GetCursorPos([ref]$saved)) { throw 'Cannot save cursor position.' }
    try {
        $settings.DeleteValue('NewTabLocation',$false)
        $settings.DeleteValue('PreferencesV1',$false)
        $defaultKeys = @('Ctrl + T','Ctrl + W','Ctrl + Tab','Ctrl + Shift + Tab')
        foreach ($action in @('Cancel','Customize','CancelReset','Reset')) {
            [void][RibbonEntryTest]::SetForegroundWindow($root)
            Wait-For "entry ready for right-click ($action)" { Test-Placement } | Out-Null
            [RibbonEntryTest]::Context($button)
            $popup = [IntPtr](Wait-For "settings context menu ($action)" { [RibbonEntryTest]::SettingsPopup($button).ToInt64() })
            [RibbonEntryTest]::Pick($popup,0)
            $popup = [IntPtr]::Zero
            $dialog = [IntPtr](Wait-For "standalone settings dialog ($action)" { [RibbonEntryTest]::SettingsDialog($button).ToInt64() })
            $selection = [Windows.Automation.SelectionItemPattern]::Pattern
            $reset = $action -in @('Reset','CancelReset')
            $current = if ($reset) { 'QuickAccess' } else { 'ThisPC' }
            if (!(SettingControl $current).GetCurrentPattern($selection).Current.IsSelected) { throw 'Dialog did not load the saved setting.' }
            for ($k=0;$k -lt 4;$k++) {
                $expected = if ($reset -and $k -eq 0) { 'F6' } else { $defaultKeys[$k] }
                if (!(SettingControl "Binding$k").Current.Name.EndsWith($expected)) { throw "Wrong shortcut for action $k" }
            }
            if ($reset) {
                (SettingControl 'Reset').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
                if (!(SettingControl 'ThisPC').GetCurrentPattern($selection).Current.IsSelected -or
                    !(SettingControl 'Binding0').Current.Name.EndsWith($defaultKeys[0])) { throw 'Reset did not restore the draft.' }
            } else {
                (SettingControl 'QuickAccess').GetCurrentPattern($selection).Select()
                (SettingControl 'Binding0').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
                Wait-For 'capture started' { (SettingControl 'Binding0').Current.Name.Contains('请按快捷键') } | Out-Null
                [RibbonEntryTest]::Stroke($dialog,0x75)
                Wait-For 'F6 recorded' { (SettingControl 'Binding0').Current.Name.EndsWith('F6') } | Out-Null
                (SettingControl 'Binding1').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
                Wait-For 'second capture started' { (SettingControl 'Binding1').Current.Name.Contains('请按快捷键') } | Out-Null
                [RibbonEntryTest]::Stroke($dialog,0x75)
                Wait-For 'duplicate rejected' { (SettingControl 'ShortcutHint').Current.Name.Contains('已用于') } | Out-Null
                [RibbonEntryTest]::Stroke($dialog,27)
                Wait-For 'Escape restored old chord' { (SettingControl 'Binding1').Current.Name.EndsWith($defaultKeys[1]) } | Out-Null
            }
            $commit = if ($action.StartsWith('Cancel')) { 'Cancel' } else { 'Save' }
            (SettingControl $commit).GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
            Wait-For 'settings closed' { ![RibbonEntryTest]::SettingsDialog($button).ToInt64() } | Out-Null
            $blob = $settings.GetValue('PreferencesV1')
            if ($action -eq 'Cancel') {
                if ($null -ne $blob) { throw 'Cancel wrote the draft.' }
            } else {
                $quick = [int]($action -ne 'Reset')
                $newKey = if ($quick) { 0x75 } else { 0x154 }
                if ([BitConverter]::ToInt32($blob,0) -ne $quick -or [BitConverter]::ToInt32($blob,4) -ne $newKey) { throw "Incorrect saved $action settings." }
                if ($action -eq 'CancelReset') { continue }
                $bar = [FixtureWindow]::GetProp($root,'EZFolder.Band.2')
                $tabs = [RibbonEntryTest]::GetDlgItem($bar,102)
                $count = [RibbonEntryTest]::TabCount($tabs)
                if (!$quick) {
                    [RibbonEntryTest]::DeliverKey($tabs,0x75)
                    if ([RibbonEntryTest]::TabCount($tabs) -ne $count) { throw 'Reset left the custom hotkey active.' }
                    continue
                }
                [RibbonEntryTest]::FocusTabs($root,$tabs)
                Wait-For 'fixture selects original folder' { $window.Document.Folder.Self.Path -eq $folder } | Out-Null
                Wait-For 'fixture tab has keyboard focus' {
                    if (![RibbonEntryTest]::Focused($tabs)) {
                        [void][RibbonEntryTest]::SetForegroundWindow($root)
                        try { [Windows.Automation.AutomationElement]::FromHandle($tabs).SetFocus() }
                        catch [System.Management.Automation.MethodInvocationException] { return $false }
                    }
                    [RibbonEntryTest]::Focused($tabs)
                } | Out-Null
                [RibbonEntryTest]::Stroke($tabs,0x75)
                Wait-For 'changed hotkey opens a Quick Access tab' { [RibbonEntryTest]::TabCount($tabs) -eq $count+1 -and $window.Document.Folder.Self.Path -match '679f85cb' } | Out-Null
            }
        }
        'PASS: four bindings, capture, conflict detection, Escape, live rebinding, reset, save and cancel.'
    } finally {
        if ($popup -ne [IntPtr]::Zero) { [RibbonEntryTest]::Key($popup,0x1b); [RibbonEntryTest]::Key($popup,0x1b) }
        $dialog = [RibbonEntryTest]::SettingsDialog($button)
        if ($dialog -ne [IntPtr]::Zero) { (SettingControl 'Cancel').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke() }
        if ($null -eq $prior) { $settings.DeleteValue('NewTabLocation',$false) }
        else { $settings.SetValue('NewTabLocation',$prior,[Microsoft.Win32.RegistryValueKind]::DWord) }
        if ($null -eq $priorPreferences) { $settings.DeleteValue('PreferencesV1',$false) }
        else { $settings.SetValue('PreferencesV1',$priorPreferences,[Microsoft.Win32.RegistryValueKind]::Binary) }
        $settings.Dispose()
        [void][RibbonEntryTest]::SetCursorPos($saved.X,$saved.Y)
    }
}
function Test-Tooltip {
    $saved = New-Object RibbonEntryTest+Point
    if (![RibbonEntryTest]::GetCursorPos([ref]$saved)) { throw 'Cannot save cursor position.' }
    $r = [RibbonEntryTest]::Bounds($button)
    try {
        [void][RibbonEntryTest]::SetCursorPos($r.Left-4,$r.Bottom+4)
        Start-Sleep -Milliseconds 120
        [void][RibbonEntryTest]::SetCursorPos(($r.Left+$r.Right)/2,($r.Top+$r.Bottom)/2)
        Wait-For 'visible tooltip containing exactly EZfolder' { [RibbonEntryTest]::Tip($button).ToInt64() } | Out-Null
    } finally { [void][RibbonEntryTest]::SetCursorPos($saved.X,$saved.Y) }
}
$folder = Join-Path $env:TEMP ('EZFolder-button-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($folder)
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
$dpiContext = [RibbonEntryTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
$window = $null
$initiallyShown = $null
try {
    $window = New-TestExplorer $folder -Separate
    $root = [IntPtr][int64]$window.HWND
    [void][RibbonEntryTest]::SetForegroundWindow($root)
    $entry = [IntPtr](Wait-For 'ribbon entry' { [RibbonEntryTest]::Entry($root).ToInt64() })
    $button = [RibbonEntryTest]::GetDlgItem($entry,1)
    $initiallyShown = [RibbonEntryTest]::Shown($root)
    if ($initiallyShown) { [RibbonEntryTest]::Click($button); Wait-For 'initial hide' { ![RibbonEntryTest]::Shown($root) } | Out-Null }
    Wait-For 'click target in the ribbon while tabs are hidden' { Test-Placement } | Out-Null
    if ($TestHover) { Test-Tooltip }
    foreach ($wanted in @($true,$false,$true)) {
        [RibbonEntryTest]::Click($button)
        Wait-For "toggle to $wanted" { [RibbonEntryTest]::Shown($root) -eq $wanted } | Out-Null
        if (![RibbonEntryTest]::IsWindowVisible($button)) { throw 'Hiding the band removed the entry.' }
    }
    'PASS: hidden band can be enabled, disabled and re-enabled from the same button.'
    if ($TestHover) { Test-Tooltip; 'PASS: real hover displayed exactly EZfolder with the band hidden and shown.' }
    $width = $window.Width
    $window.Width = [math]::Max(480,$width-180)
    Wait-For 'right edge after resize' { $window.Width -ne $width -and (Test-Placement) } | Out-Null
    $window.Width = $width
    Wait-For 'right edge at original width' { Test-Placement } | Out-Null
    'PASS: the button follows the ribbon edge and receives hits at both widths.'
    if ($TestSettings) { Test-SettingsMenu }
    if ($PreviewPath) {
        Add-Type -AssemblyName System.Drawing
        $dock = [RibbonEntryTest]::Bounds([RibbonEntryTest]::GetParent($entry))
        $r = [RibbonEntryTest]::Bounds($button)
        $bitmap = [Drawing.Bitmap]::new($dock.Right-$dock.Left,$r.Bottom-$dock.Top)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.CopyFromScreen($dock.Left,$dock.Top,0,0,$bitmap.Size)
            $bitmap.Save($PreviewPath,[Drawing.Imaging.ImageFormat]::Png)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
} finally {
    try {
        if ($window -and $null -ne $initiallyShown -and [RibbonEntryTest]::Shown($root) -ne $initiallyShown) {
            $window.ShowBrowserBar('{5129B68D-A44B-427B-AEE1-53137D3989EA}',[bool]$initiallyShown,$null)
            Wait-For 'restore initial display state' { [RibbonEntryTest]::Shown($root) -eq $initiallyShown } | Out-Null
        }
    } finally {
        if ($window) { $window.Quit() }
        [void][RibbonEntryTest]::SetThreadDpiAwarenessContext($dpiContext)
        [IO.Directory]::Delete($folder)
    }
}
