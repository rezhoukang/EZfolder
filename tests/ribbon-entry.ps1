param([string]$PreviewPath,[switch]$TestHover,[switch]$TestSettings,[switch]$TestTabActions,[switch]$TestRouting)
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
    [DllImport("user32")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
    public static void RestoreShortcut(IntPtr root,IntPtr tabs) {
        if(GetForegroundWindow()!=root || Focused(tabs)) throw new Exception("Expected focus in the Explorer window outside the tab strip.");
        foreach(byte key in new byte[]{17,16,84}) keybd_event(key,0,0,UIntPtr.Zero);
        foreach(byte key in new byte[]{84,16,17}) keybd_event(key,0,2,UIntPtr.Zero);
    }
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
    [DllImport("user32")] static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32")] static extern void mouse_event(uint flags,int dx,int dy,uint data,UIntPtr extra);
    public static void Pick(IntPtr popup,int pos) {
        IntPtr menu; Rect rect;
        if (SendMessageTimeout(popup,0x1E1,IntPtr.Zero,IntPtr.Zero,2,300,out menu)==IntPtr.Zero ||
            !GetMenuItemRect(IntPtr.Zero,menu,pos,out rect)) throw new Exception("Cannot locate choice");
        if (GetMenuItemCount(menu)!=4) throw new Exception("Expected three EZFolder commands and a separator");
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
    [DllImport("user32")] public static extern bool IsWindowEnabled(IntPtr w);
    public static void NewTab(IntPtr bar) { PostMessage(bar,0x111,(IntPtr)100,IntPtr.Zero); }
    [DllImport("user32")] static extern IntPtr GetAncestor(IntPtr w,uint flag);
    public static void StatusMenu(IntPtr root,Point point) {
        if(GetAncestor(WindowFromPoint(point),2)!=root) throw new Exception("Status bar is covered.");
        SetCursorPos(point.X,point.Y);
        mouse_event(8,0,0,0,UIntPtr.Zero);
        mouse_event(16,0,0,0,UIntPtr.Zero);
    }
    public static void OpenItem(IntPtr root,Point point) {
        if(GetAncestor(WindowFromPoint(point),2)!=root) throw new Exception("Folder item is covered.");
        SetCursorPos(point.X,point.Y);
        for (int i=0;i<2;++i) {
            mouse_event(2,0,0,0,UIntPtr.Zero);
            mouse_event(4,0,0,0,UIntPtr.Zero);
            System.Threading.Thread.Sleep(40);
        }
    }
    public static void Menu(IntPtr w) { PostMessage(w,0x7b,w,new IntPtr(-1)); }
    public static void Mouse(IntPtr w,int message,int x,int y) {
        IntPtr result;
        if(SendMessageTimeout(w,message,(IntPtr)(message==0x201||message==0x200?1:0),(IntPtr)((y<<16)|(x&65535)),2,6000,out result)==IntPtr.Zero)
            throw new Exception("Test window did not respond to mouse input");
    }
    public static bool DragTip(IntPtr bar) {
        uint owner,pid; GetWindowThreadProcessId(bar,out owner);
        for(IntPtr w=FindWindowEx(IntPtr.Zero,IntPtr.Zero,"tooltips_class32",null);w!=IntPtr.Zero;w=FindWindowEx(IntPtr.Zero,w,"tooltips_class32",null)) {
            GetWindowThreadProcessId(w,out pid);
            if(pid!=owner||!IsWindowVisible(w))continue;
            var text=new StringBuilder(128);IntPtr result;
            if(ReadText(w,13,(IntPtr)128,text,2,300,out result)!=IntPtr.Zero && text.ToString().Contains("合并"))return true;
        }
        return false;
    }
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
function Test-TabActions {
    $shell=New-Object -ComObject Shell.Application
    $bar=[FixtureWindow]::GetProp($root,'EZFolder.Band.2')
    $tabs=[RibbonEntryTest]::GetDlgItem($bar,102)
    function Pick-Action($surface,$position) {
        if ($surface -eq $root) { [RibbonEntryTest]::StatusMenu($root,$point) }
        else { [RibbonEntryTest]::Menu($surface) }
        $popup=[IntPtr](Wait-For 'EZFolder commands' { [RibbonEntryTest]::SettingsPopup($tabs).ToInt64() })
        [RibbonEntryTest]::Pick($popup,$position)
    }
    Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
    $frame=[Windows.Automation.AutomationElement]::FromHandle($root)
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::StatusBar)
    $status=$frame.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
    if (!$status) { throw 'Explorer status bar missing.' }
    $r=$status.Current.BoundingRectangle
    $point=[RibbonEntryTest+Point]::new($r.Left+60,$r.Top+$r.Height/2)
    $child=Join-Path $folder 'Moved'
    [void][IO.Directory]::CreateDirectory($child)
    $other=$null
    try {
        [RibbonEntryTest]::NewTab($bar)
        Wait-For 'new tab ready' { [RibbonEntryTest]::TabCount($tabs) -eq 2 -and [RibbonEntryTest]::IsWindowEnabled($tabs) } | Out-Null
        $window.Navigate2($folder)
        Wait-For 'test folder in second tab' { $window.Document.Folder.Self.Path -eq $folder } | Out-Null
        $name=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Moved')
        $item=Wait-For 'visible child folder' { $frame.FindFirst([Windows.Automation.TreeScope]::Descendants,$name) }
        $hit=$item.GetClickablePoint()
        [RibbonEntryTest]::OpenItem($root,[RibbonEntryTest+Point]::new($hit.X,$hit.Y))
        Wait-For 'subfolder in second tab' { $window.Document.Folder.Self.Path -eq $child } | Out-Null
        if ([RibbonEntryTest]::TabCount($tabs) -ne 2) { throw 'Opening a subfolder created another tab.' }
        Start-Sleep -Milliseconds 550
        $dpi=[RibbonEntryTest]::GetDpiForWindow($tabs)
        [RibbonEntryTest]::Mouse($tabs,0x208,[int](144*$dpi/96),8)
        Wait-For 'tab closed' { [RibbonEntryTest]::TabCount($tabs) -eq 1 -and $window.Document.Folder.Self.Path -eq $folder } | Out-Null
        [RibbonEntryTest]::RestoreShortcut($root,$tabs)
        Wait-For 'Ctrl+Shift+T restored closed directory' {
            [RibbonEntryTest]::TabCount($tabs) -eq 2 -and $window.Document.Folder.Self.Path -eq $child -and [RibbonEntryTest]::IsWindowEnabled($tabs)
        } | Out-Null
        [RibbonEntryTest]::Mouse($tabs,0x208,[int](144*$dpi/96),8)
        Wait-For 'restored tab closed again' { [RibbonEntryTest]::TabCount($tabs) -eq 1 -and $window.Document.Folder.Self.Path -eq $folder } | Out-Null
        Pick-Action $root 3
        Wait-For 'restored closed directory' { [RibbonEntryTest]::TabCount($tabs) -eq 2 -and $window.Document.Folder.Self.Path -eq $child } | Out-Null
        Pick-Action $tabs 2
        $other=Wait-For 'independent copy' { $shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $child } | Select-Object -First 1 }
        if ([RibbonEntryTest]::TabCount($tabs) -ne 2) { throw 'Opening in a new window removed the original tab.' }
        $otherRoot=[IntPtr][long]$other.HWND
        $otherBar=[IntPtr](Wait-For 'second window band' { [FixtureWindow]::GetProp($otherRoot,'EZFolder.Band.2').ToInt64() } )
        $otherTabs=[RibbonEntryTest]::GetDlgItem($otherBar,102)
        $window.Left=0; $window.Top=60; $window.Width=560
        $other.Left=580; $other.Top=60; $other.Width=560
        Start-Sleep -Milliseconds 500
        $from=[RibbonEntryTest]::Bounds($tabs); $to=[RibbonEntryTest]::Bounds($otherTabs)
        $x=$to.Left+15-$from.Left; $y=$to.Top+8-$from.Top
        [RibbonEntryTest]::Mouse($tabs,0x201,12,8)
        [RibbonEntryTest]::Mouse($tabs,0x200,$x,$y)
        Wait-For 'drag feedback identifies merge target' { [RibbonEntryTest]::DragTip($bar) } | Out-Null
        [RibbonEntryTest]::Mouse($tabs,0x202,$x,$y)
        Start-Sleep -Milliseconds 250
        Wait-For 'explicit drop moves only the dragged folder' {
            [RibbonEntryTest]::TabCount($tabs) -eq 1 -and [RibbonEntryTest]::TabCount($otherTabs) -eq 2 -and
                $window.Document.Folder.Self.Path -eq $child -and $other.Document.Folder.Self.Path -eq $folder
        } | Out-Null
        'PASS: status bar restore, new window from tab menu, drag preview and acknowledged cross-window move.'
        if ($TestRouting) {
            $competing=@($shell.Windows() | Where-Object { $_.HWND -notin @($window.HWND,$other.HWND) -and [RibbonEntryTest]::Shown([IntPtr][long]$_.HWND) })
            if ($competing.Count) { throw 'Routing test needs two isolated Explorer frames.' }
            function Open-RoutedFolder($name,$receiver,$strip,[switch]$Explore) {
                $path=Join-Path $folder $name
                [void][IO.Directory]::CreateDirectory($path)
                $count=[RibbonEntryTest]::TabCount($strip)
                if ($Explore) { $shell.Explore($path) } else { $shell.ShellExecute($path) }
                Wait-For "folder open into $name" { [RibbonEntryTest]::TabCount($strip) -eq $count+1 -and $receiver.Document.Folder.Self.Path -eq $path } | Out-Null
            }
            [void][RibbonEntryTest]::SetForegroundWindow($root)
            Open-RoutedFolder 'Primary' $other $otherTabs
            if (![FixtureWindow]::GetProp($otherBar,'EZFolder.Primary').ToInt64()) { throw 'The larger window was not marked primary.' }
            $other.Width=480
            $plus=[RibbonEntryTest]::GetDlgItem($otherBar,100)
            for ($i=0;$i -lt 64 -and ![FixtureWindow]::GetProp($otherBar,'EZFolder.Full').ToInt64();$i++) {
                Wait-For 'enabled plus' { [RibbonEntryTest]::IsWindowEnabled($plus) } | Out-Null
                $n=[RibbonEntryTest]::TabCount($otherTabs)
                [RibbonEntryTest]::NewTab($otherBar)
                Wait-For 'additional tab' { [RibbonEntryTest]::TabCount($otherTabs) -eq $n+1 } | Out-Null
            }
            if (![FixtureWindow]::GetProp($otherBar,'EZFolder.Full').ToInt64()) {throw 'Fixture did not reach capacity.'}
            $fullCount=[RibbonEntryTest]::TabCount($otherTabs)
            $existing=Join-Path $folder 'Primary'
            $shell.Explore($existing)
            Wait-For 'full primary reuses its own tab' { [RibbonEntryTest]::TabCount($otherTabs) -eq $fullCount -and $other.Document.Folder.Self.Path -eq $existing } | Out-Null
            Open-RoutedFolder 'Overflow' $window $tabs -Explore
            $other.Width=900
            Wait-For 'primary has room again' { ![FixtureWindow]::GetProp($otherBar,'EZFolder.Full').ToInt64() } | Out-Null
            Open-RoutedFolder 'BackToPrimary' $other $otherTabs
            'PASS: external default open, primary election, Explore overflow and return to primary.'
        }
    } finally {
        if ($other) { $other.Quit() }
        if ($TestRouting) {
            foreach ($name in 'Primary','Overflow','BackToPrimary') {
                $path=Join-Path $folder $name
                if ([IO.Directory]::Exists($path)) { [IO.Directory]::Delete($path) }
            }
        }
        if ([IO.Directory]::Exists($child)) { [IO.Directory]::Delete($child) }
    }
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
        $defaultKeys = @('Ctrl + T','Ctrl + W','Ctrl + Tab','Ctrl + Shift + Tab','Ctrl + Shift + T')
        foreach ($action in @('Cancel','Customize','CancelReset','Reset')) {
            [void][RibbonEntryTest]::SetForegroundWindow($root)
            Wait-For "entry ready for right-click ($action)" { Test-Placement } | Out-Null
            $band=[FixtureWindow]::GetProp($root,'EZFolder.Band.2')
            $strip=[RibbonEntryTest]::GetDlgItem($band,102)
            [RibbonEntryTest]::Context($strip)
            $popup = [IntPtr](Wait-For "settings context menu ($action)" { [RibbonEntryTest]::SettingsPopup($strip).ToInt64() })
            [RibbonEntryTest]::Pick($popup,0)
            $popup = [IntPtr]::Zero
            $dialog = [IntPtr](Wait-For "standalone settings dialog ($action)" { [RibbonEntryTest]::SettingsDialog($button).ToInt64() })
            $selection = [Windows.Automation.SelectionItemPattern]::Pattern
            $resetRect=(SettingControl 'Reset').Current.BoundingRectangle
            $rowRect=(SettingControl 'Binding0').Current.BoundingRectangle
            if ([Math]::Abs($resetRect.Right-$rowRect.Right) -gt 2) { throw 'Reset is not aligned to the right of the shortcut column.' }
            $reset = $action -in @('Reset','CancelReset')
            $current = if ($reset) { 'QuickAccess' } else { 'ThisPC' }
            if (!(SettingControl $current).GetCurrentPattern($selection).Current.IsSelected) { throw 'Dialog did not load the saved setting.' }
            for ($k=0;$k -lt $defaultKeys.Count;$k++) {
                $expected = if ($reset -and $k -eq 0) { 'F6' } elseif ($reset -and $k -eq 4) { 'F8' } elseif ($reset) { '-' } else { $defaultKeys[$k] }
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
                Wait-For 'Escape clears the binding' { (SettingControl 'Binding1').Current.Name.EndsWith('-') } | Out-Null
                foreach ($binding in 2,3) {
                    $key = if ($binding -eq 2) { 46 } else { 8 }
                    (SettingControl "Binding$binding").GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
                    Wait-For "start clearing binding $binding" { (SettingControl "Binding$binding").Current.Name.Contains('请按快捷键') } | Out-Null
                    [RibbonEntryTest]::Stroke($dialog,$key)
                    Wait-For "key $key clears the binding" { (SettingControl "Binding$binding").Current.Name.EndsWith('-') } | Out-Null
                }
                (SettingControl 'Binding4').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
                Wait-For 'restore shortcut capture' { (SettingControl 'Binding4').Current.Name.Contains('请按快捷键') } | Out-Null
                [RibbonEntryTest]::Stroke($dialog,0x77)
                Wait-For 'restore rebound to F8' { (SettingControl 'Binding4').Current.Name.EndsWith('F8') } | Out-Null
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
                $restoreKey = if ($quick) { 0x77 } else { 0x354 }
                foreach ($binding in 1,2,3) {
                    $expectedKey = if ($quick) { 0 } else { @(0x157,0x109,0x309)[$binding-1] }
                    if ([BitConverter]::ToInt32($blob,4*($binding+1)) -ne $expectedKey) { throw "Incorrect cleared/reset binding $binding." }
                }
                if ($blob.Length -ne 24 -or [BitConverter]::ToInt32($blob,20) -ne $restoreKey) { throw 'Incorrect saved restore binding.' }
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
        'PASS: five bindings, capture, conflict detection, Escape, live rebinding, reset, save and cancel.'
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
function Test-OpenWithoutReceiver {
    $shell=New-Object -ComObject Shell.Application
    foreach ($action in 'default','explore') {
        if (@($shell.Windows() | Where-Object { [RibbonEntryTest]::Shown([IntPtr][long]$_.HWND) }).Count) {
            throw 'Fallback test needs all receiving bands hidden.'
        }
        $path=Join-Path $folder "Fallback-$action"
        [void][IO.Directory]::CreateDirectory($path)
        try {
            if ($action -eq 'explore') { $shell.Explore($path) } else { $shell.ShellExecute($path) }
            $child=Wait-For "native $action without a receiver" {
                $shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $path } | Select-Object -First 1
            }
            $handle=$child.HWND
            $child.Quit()
            Wait-For 'fallback window closed' { !@($shell.Windows() | Where-Object { $_.HWND -eq $handle }).Count } | Out-Null
        } finally { [IO.Directory]::Delete($path) }
    }
    'PASS: default open and explicit Explore opened a native window with the receiving band hidden.'
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
    if ($TestRouting) { Test-OpenWithoutReceiver }
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
    if ($TestTabActions -or $TestRouting) { Test-TabActions }
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
