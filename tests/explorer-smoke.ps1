# Run with 64-bit Windows PowerShell after installing the band.
param([switch]$RequireAlreadyVisible,[switch]$SeparateProcess,[switch]$MeasurePresentation,[switch]$TestDensity,[switch]$TestPlusPaint)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
$id = '{5129B68D-A44B-427B-AEE1-53137D3989EA}'
Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BandTest {
    delegate bool Visit(IntPtr w, IntPtr p);
    [DllImport("user32")] static extern bool EnumChildWindows(IntPtr w, Visit visit, IntPtr p);
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr w, int id);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr w, int m, IntPtr a, IntPtr b, int flags, int timeout, out IntPtr result);
    public static IntPtr Find(IntPtr root) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (w,p) => {
            if (!IsWindowVisible(w) || GetDlgItem(w,100) == IntPtr.Zero || GetDlgItem(w,103) != IntPtr.Zero || GetDlgItem(w,102) == IntPtr.Zero) return true;
            found = w; return false;
        }, IntPtr.Zero);
        return found;
    }
    public static int Send(IntPtr w, int m, int a, int b) {
        IntPtr r;
        if (SendMessageTimeout(w,m,(IntPtr)a,(IntPtr)b,2,3000,out r) == IntPtr.Zero) throw new Exception("Window message timed out or failed");
        return r.ToInt32();
    }
    public static int Count(IntPtr bar) { return Send(GetDlgItem(bar,102),0x1304,0,0); }
    public static int Index(IntPtr bar) { return Send(GetDlgItem(bar,102),0x130B,0,0); }
    public struct Rect { public int Left, Top, Right, Bottom; }
    public struct Point { public int X,Y; }
    [DllImport("user32")] static extern bool GetClientRect(IntPtr w,out Rect rect);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr w,out Rect rect);
    public static Rect Bounds(IntPtr w) { Rect r; GetWindowRect(w,out r); return r; }
    [DllImport("user32")] public static extern bool IsWindowEnabled(IntPtr w);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string name);
    public static bool Paging(IntPtr tabs) { return FindWindowEx(tabs,IntPtr.Zero,"msctls_updown32",null)!=IntPtr.Zero; }
    delegate void WinEvent(IntPtr hook,uint ev,IntPtr w,int obj,int child,uint thread,uint time);
    [DllImport("user32")] static extern IntPtr SetWinEventHook(uint first,uint last,IntPtr mod,WinEvent callback,uint pid,uint tid,uint flags);
    [DllImport("user32")] static extern bool UnhookWinEvent(IntPtr hook);
    [DllImport("user32")] static extern IntPtr GetParent(IntPtr w);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,System.Text.StringBuilder name,int capacity);
    static IntPtr watched, eventHook;
    static int pagerShows;
    static readonly WinEvent observe=(h,e,w,obj,child,t,ms)=>{
        if((e!=0x8000 && e!=0x8002) || obj!=0 || GetParent(w)!=watched) return;
        var name=new System.Text.StringBuilder(64);
        GetClassName(w,name,name.Capacity);
        if(name.ToString()=="msctls_updown32") ++pagerShows;
    };
    public static void WatchPager(IntPtr tabs) {
        uint pid; uint tid=GetWindowThreadProcessId(tabs,out pid);
        watched=tabs; pagerShows=0;
        eventHook=SetWinEventHook(0x8000,0x8002,IntPtr.Zero,observe,pid,tid,0);
        if(eventHook==IntPtr.Zero) throw new Exception("Could not observe pager visibility.");
    }
    public static int StopWatchingPager() {
        System.Windows.Forms.Application.DoEvents();
        if(eventHook!=IntPtr.Zero) UnhookWinEvent(eventHook);
        eventHook=IntPtr.Zero;
        return pagerShows;
    }
    public static int ItemWidth(IntPtr tabs) { Rect r; GetClientRect(tabs,out r); int dpi=GetDpiForWindow(tabs); return Math.Min(132*dpi/96,(r.Right-6*dpi/96)/Send(tabs,0x1304,0,0)); }
    [DllImport("user32")] public static extern bool GetCursorPos(out Point p);
    [DllImport("user32")] public static extern bool SetCursorPos(int x,int y);
    [DllImport("user32")] public static extern bool SetForegroundWindow(IntPtr w);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int RegisterWindowMessage(string name);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)]
    struct CopyData { public IntPtr type; public int bytes; [MarshalAs(UnmanagedType.LPWStr)] public string path; }
    [DllImport("user32",EntryPoint="SendMessageTimeoutW")] static extern IntPtr SendPath(IntPtr w,int msg,IntPtr from,ref CopyData packet,int flags,int timeout,out IntPtr result);
    public static bool AcceptsFolder(IntPtr target,string path) {
        var packet=new CopyData {type=(IntPtr)RegisterWindowMessage("EZFolder.OpenFolder.4"),bytes=(path.Length+1)*2,path=path};
        IntPtr result;
        if (SendPath(target,0x4A,IntPtr.Zero,ref packet,2,3000,out result)==IntPtr.Zero)
            throw new Exception("Folder receiver did not respond.");
        return result!=IntPtr.Zero;
    }
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    [DllImport("user32",CharSet=CharSet.Unicode,EntryPoint="SendMessageTimeoutW")]
    static extern IntPtr ReadText(IntPtr w,int m,IntPtr a,System.Text.StringBuilder b,int flags,int ms,out IntPtr result);
    public static bool EmptyStrip(IntPtr bar) {
        return Count(bar)==0 && Index(bar)==-1 && !IsWindowVisible(GetDlgItem(bar,102)) &&
            GetDlgItem(bar,104)==IntPtr.Zero && IsWindowVisible(GetDlgItem(bar,100));
    }
    public static bool HasTitle(IntPtr tabs,string title) {
        uint owner,pid; GetWindowThreadProcessId(tabs,out owner);
        for (IntPtr w=FindWindowEx(IntPtr.Zero,IntPtr.Zero,"tooltips_class32",null);w!=IntPtr.Zero;w=FindWindowEx(IntPtr.Zero,w,"tooltips_class32",null)) {
            GetWindowThreadProcessId(w,out pid);
            if (pid!=owner || !IsWindowVisible(w) || Bounds(w).Top < Bounds(tabs).Bottom) continue;
            var text=new System.Text.StringBuilder(512); IntPtr result;
            if (ReadText(w,13,(IntPtr)text.Capacity,text,2,300,out result)!=IntPtr.Zero && text.ToString()==title) return true;
        }
        return false;
    }
    [DllImport("user32")] public static extern int GetDpiForWindow(IntPtr w);
    public static void ClickTab(IntPtr bar, int x) {
        IntPtr tabs = GetDlgItem(bar,102);
        int point = (8 << 16) | x;
        Send(tabs,0x201,1,point);
        Send(tabs,0x202,0,point);
    }
    [DllImport("user32")] static extern bool PostMessage(IntPtr w,int msg,IntPtr wp,IntPtr lp);
    [DllImport("user32")] static extern IntPtr GetDC(IntPtr w);
    [DllImport("user32")] static extern int ReleaseDC(IntPtr w,IntPtr dc);
    [DllImport("user32")] static extern IntPtr WindowFromPoint(Point p);
    [DllImport("user32")] static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
    public static void Topmost(IntPtr w,bool on) {
        if (!SetWindowPos(w,new IntPtr(on ? -1 : -2),0,0,0,0,0x13)) throw new Exception("Fixture placement failed.");
    }
    [DllImport("gdi32")] static extern uint GetPixel(IntPtr dc,int x,int y);
    [DllImport("dwmapi")] static extern int DwmFlush();
    public static int CheckPlusDuringSwitch(IntPtr bar,int index) {
        IntPtr tabs=GetDlgItem(bar,102),plus=GetDlgItem(bar,100);
        Rect r=Bounds(plus);
        Point p=new Point { X=(r.Left+r.Right-2)/2,Y=(r.Top+r.Bottom-2)/2 };
        if (WindowFromPoint(p)!=plus) throw new Exception("Plus is covered.");
        IntPtr dc=GetDC(IntPtr.Zero);
        try {
            DwmFlush();
            uint ink=GetPixel(dc,p.X,p.Y);
            if (ink==0xffffffff || ink==GetPixel(dc,r.Left+1,r.Top+1)) throw new Exception("Plus glyph not found.");
            IntPtr click=(IntPtr)((8<<16)|(ItemWidth(tabs)*index+12));
            if (!PostMessage(tabs,0x201,(IntPtr)1,click) || !PostMessage(tabs,0x202,IntPtr.Zero,click))
                throw new Exception("Could not switch tabs.");
            int settled=0, samples=0;
            var clock=System.Diagnostics.Stopwatch.StartNew();
            do {
                DwmFlush();
                if (GetPixel(dc,p.X,p.Y)!=ink) throw new Exception("Plus glyph changed brightness during a tab switch.");
                ++samples;
                settled=IsWindowEnabled(tabs) && Index(bar)==index ? settled+1 : 0;
            } while (settled<3 && clock.ElapsedMilliseconds<4000);
            if (settled<3) throw new Exception("Tab switch did not finish.");
            return samples;
        } finally { ReleaseDC(IntPtr.Zero,dc); }
    }
}
'@
function Wait-For($description, [scriptblock]$check) {
    $until = [datetime]::UtcNow.AddSeconds(12)
    do {
        $value = & $check
        if ($value) { return $value }
        Start-Sleep -Milliseconds 150
    } while ([datetime]::UtcNow -lt $until)
    if ($bar) { throw "Timed out: $description (folder=$($window.Document.Folder.Self.Path), tabs=$([BandTest]::Count($bar)), selected=$([BandTest]::Index($bar)))" }
    throw "Timed out: $description"
}
$folder = Join-Path ([IO.Path]::GetTempPath()) ('EZFolder-' + [guid]::NewGuid().ToString('N').Substring(0,8))
$child = Join-Path $folder 'Child'
$burst = @((Join-Path $folder 'One'), (Join-Path $folder 'Two'))
$measuredFolders = @()
[IO.Directory]::CreateDirectory($child) | Out-Null
$shell = New-Object -ComObject Shell.Application
$window = $null
$testWindows = @()
$settings = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\EZFolder')
$prior = $settings.GetValue('NewTabLocation')
$priorPreferences = $settings.GetValue('PreferencesV1')
function Open-TestFolder($path) {
    if ($SeparateProcess) { Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "/separate,`"$path`"" -WindowStyle Normal }
    else { $shell.Explore($path) }
}
function Test-Density {
    $tabs = [BandTest]::GetDlgItem($bar,102)
    $plus = [BandTest]::GetDlgItem($bar,100)
    $width = $window.Width
    $saved = New-Object BandTest+Point
    $dpiContext = [BandTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
    [void][BandTest]::GetCursorPos([ref]$saved)
    try {
        [BandTest]::WatchPager($tabs)
        [BandTest]::Topmost($root,$true)
        $window.Width = 760
        $r = [BandTest]::Bounds($bar)
        $dpi = [BandTest]::GetDpiForWindow($tabs)
        $capacity = [math]::Floor((($r.Right-$r.Left) - [math]::Round(32*$dpi/96) - [math]::Round(6*$dpi/96)) / [math]::Round(24*$dpi/96))
        for ($i=2; $i -le $capacity; $i++) {
            [void][BandTest]::Send($bar,0x111,100,0)
            Wait-For "create tab $i" { [BandTest]::Count($bar) -eq $i -and [BandTest]::Index($bar) -eq $i-1 } | Out-Null
        }
        [void][BandTest]::Send($bar,0x111,100,0)
        if ([BandTest]::IsWindowEnabled($plus) -or [BandTest]::Count($bar) -ne $capacity -or [BandTest]::Paging($tabs) -or [BandTest]::Send($tabs,0x132c,0,0) -ne 1) { throw 'Capacity, row count or scrolling regression.' }
        if ([BandTest]::Bounds($plus).Right -ne $r.Right) { throw 'Plus moved away from the right edge.' }
        $tr = [BandTest]::Bounds($tabs)
        $pt = New-Object BandTest+Point
        $pt.X = $tr.Left+6; $pt.Y = ($tr.Top+$tr.Bottom)/2
        [void][BandTest]::SetForegroundWindow($root)
        [void][BandTest]::SetCursorPos($pt.X,$tr.Bottom+10)
        Start-Sleep -Milliseconds 150
        [void][BandTest]::SetCursorPos($pt.X,$pt.Y)
        Wait-For 'full truncated title below the tab' { [BandTest]::HasTitle($tabs,[IO.Path]::GetFileName($folder)) } | Out-Null
        $otherBands = @($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and [FixtureWindow]::GetProp([IntPtr][long]$_.HWND,'EZFolder.Band.2') -ne [IntPtr]::Zero })
        if (!$otherBands.Count) {
            Open-TestFolder $child
            $other = Wait-For 'full window leaves ordinary new folder independent' { $shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $child } | Select-Object -First 1 }
            try {
                Start-Sleep -Milliseconds 1500
                if ($other.Document.Folder.Self.Path -ne $child -or [BandTest]::Count($bar) -ne $capacity) { throw 'Full target accepted an external folder.' }
            } finally { $other.Quit() }
            'PASS: a full window declined an ordinary external open.'
        } else {
            # Address the full fixture directly so the user's existing frames cannot receive the test folder.
            if ([BandTest]::AcceptsFolder($bar,$child) -or [BandTest]::Count($bar) -ne $capacity) { throw 'Full window accepted a new directory.' }
            'PASS: full window declined a new directory sent directly to it.'
        }
        $window.Width = 510
        Wait-For 'shrink preserves every tab in a single row without a paging control' { [BandTest]::Bounds($bar).Right-[BandTest]::Bounds($bar).Left -lt $r.Right-$r.Left -and ![BandTest]::Paging($tabs) -and [BandTest]::Send($tabs,0x132c,0,0) -eq 1 -and [BandTest]::Count($bar) -eq $capacity } | Out-Null
        $window.Width = $width
        while ([BandTest]::Count($bar) -gt 1) {
            $count = [BandTest]::Count($bar)
            $w = [BandTest]::ItemWidth($tabs)
            [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor ($w+8)))
            Wait-For 'release tab slot' { [BandTest]::Count($bar) -eq $count-1 } | Out-Null
        }
        if (![BandTest]::IsWindowEnabled($plus)) { throw 'Add did not re-enable.' }
        $shows = [BandTest]::StopWatchingPager()
        if ($shows) { throw "Native right-edge pager emitted $shows creation/show events during layout." }
        "PASS: $capacity tabs in a $($r.Right-$r.Left) pixel band, fixed plus, no paging, full hover title, resize and release."
    } finally {
        [void][BandTest]::StopWatchingPager()
        [BandTest]::Topmost($root,$false)
        [void][BandTest]::SetCursorPos($saved.X,$saved.Y)
        [void][BandTest]::SetThreadDpiAwarenessContext($dpiContext)
        $window.Width = $width
    }
}
try {
    $settings.DeleteValue('NewTabLocation',$false)
    $settings.DeleteValue('PreferencesV1',$false)
    $window = New-TestExplorer $folder -Separate:$SeparateProcess
    $null = Wait-For 'new Explorer test window' {
        $shell.Windows() | Where-Object { $_.Document.Folder.Self.Path -eq $folder } | Select-Object -First 1
    }
    $root = [IntPtr][int64]$window.HWND
    if (!$RequireAlreadyVisible -and ![BandTest]::Find($root).ToInt64()) {
        $window.ShowBrowserBar($id, $false, $null)
        $window.ShowBrowserBar($id, $true, $null)
    }
    $bar = Wait-For 'EZFolder desk band inside Explorer' { [BandTest]::Find($root).ToInt64() }
    $bar = [IntPtr]$bar
    Wait-For 'initial folder tab' { [BandTest]::Count($bar) -eq 1 } | Out-Null
    if ($TestDensity) { Test-Density }
    $plus = [BandTest]::GetDlgItem($bar,100)
    [BandTest]::Send($plus,0xF5,0,0) | Out-Null # BM_CLICK exercises the button's pressed and released states.
    Wait-For 'second tab' { [BandTest]::Count($bar) -eq 2 -and [BandTest]::Index($bar) -eq 1 } | Out-Null
    $pc = $shell.NameSpace(17).Self.Path
    Wait-For 'plus opens This PC by default' { $window.Document.Folder.Self.Path -eq $pc } | Out-Null
    if ($TestPlusPaint) {
        $ctx=[BandTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
        try {
            $window.Left=40; $window.Top=80; $window.Width=900; $window.Height=560
            [BandTest]::Topmost($root,$true)
            [void][BandTest]::SetForegroundWindow($root)
            Wait-For 'initial view ready' { [BandTest]::IsWindowEnabled($plus) -and [BandTest]::IsWindowEnabled([BandTest]::GetDlgItem($bar,102)) } | Out-Null
            $samples=0
            for ($i=0;$i -lt 12;$i++) {
                $index=$i%2
                $samples+=[BandTest]::CheckPlusDuringSwitch($bar,$index)
                $expected=if ($index) { $pc } else { $folder }
                Wait-For 'switched directory' { $window.Document.Folder.Self.Path -eq $expected } | Out-Null
            }
            "PASS: plus glyph stayed stable over 12 tab switches ($samples screen samples)."
        } finally { [BandTest]::Topmost($root,$false); [void][BandTest]::SetThreadDpiAwarenessContext($ctx) }
    }
    $settings.SetValue('NewTabLocation',1,[Microsoft.Win32.RegistryValueKind]::DWord)
    [void][BandTest]::Send($bar,0x111,100,0)
    Wait-For 'plus opens Quick Access after setting change' {
        [BandTest]::Count($bar) -eq 3 -and $window.Document.Folder.Self.Path -match '679f85cb'
    } | Out-Null
    $dpi = [BandTest]::GetDpiForWindow($bar)
    $tabs = [BandTest]::GetDlgItem($bar,102)
    # Close the inactive first tab's neighbour with its own X.
    $tabWidth = [BandTest]::ItemWidth($tabs)
    [BandTest]::ClickTab($bar,2*$tabWidth-[int](8*$dpi/96))
    Wait-For 'inactive close keeps Quick Access selected' {
        [BandTest]::Count($bar) -eq 2 -and $window.Document.Folder.Self.Path -match '679f85cb'
    } | Out-Null
    $window.Navigate2($child)
    Wait-For 'child folder' { $window.Document.Folder.Self.Path -eq $child } | Out-Null
    [BandTest]::ClickTab($bar,12)
    Wait-For 'first tab restores original directory' { $window.Document.Folder.Self.Path -eq $folder -and [BandTest]::Index($bar) -eq 0 } | Out-Null
    # Drag the inactive child tab out of the strip with one left press and release.
    $point = (8 -shl 16) -bor ($tabWidth+20)
    [void][BandTest]::Send($tabs,0x201,1,$point)
    [void][BandTest]::Send($tabs,0x200,1,((80 -shl 16) -bor ($tabWidth+20)))
    [void][BandTest]::Send($tabs,0x202,0,((80 -shl 16) -bor ($tabWidth+20)))
    $independent = Wait-For 'detached tab has its own frame' {
        $shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $child } | Select-Object -First 1
    }
    $testWindows += $independent
    Wait-For 'source tab removed only after detached folder is open' { [BandTest]::Count($bar) -eq 1 } | Out-Null
    Start-Sleep -Milliseconds 1300
    if ($independent.Document.Folder.Self.Path -ne $child -or $window.Document.Folder.Self.Path -ne $folder) {
        throw "Separation did not persist: detached=$($independent.Document.Folder.Self.Path) source=$($window.Document.Folder.Self.Path), source tabs=$([BandTest]::Count($bar))"
    }
    $independent.Quit()
    $testWindows = @()
    [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor 12))
    Wait-For 'middle-close leaves Quick Access with a blank strip' { $window.Document.Folder.Self.Path -match '679f85cb' -and [BandTest]::EmptyStrip($bar) } | Out-Null
    $window.Navigate2($child)
    Wait-For 'leaving Quick Access creates one selected directory tab' {
        $window.Document.Folder.Self.Path -eq $child -and [BandTest]::Count($bar) -eq 1 -and [BandTest]::Index($bar) -eq 0 -and [BandTest]::IsWindowVisible($tabs)
    } | Out-Null
    [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor 12))
    Wait-For 'closing the new directory tab returns to blank Quick Access' { $window.Document.Folder.Self.Path -match '679f85cb' -and [BandTest]::EmptyStrip($bar) } | Out-Null
    [void][BandTest]::Send($plus,0xF5,0,0)
    Wait-For 'plus creates the first real tab even at the same location' { [BandTest]::Count($bar) -eq 1 -and [BandTest]::Index($bar) -eq 0 -and [BandTest]::IsWindowVisible($tabs) } | Out-Null
    $settings.SetValue('NewTabLocation',0,[Microsoft.Win32.RegistryValueKind]::DWord)
    [BandTest]::ClickTab($bar,[BandTest]::ItemWidth($tabs)-[int](8*$dpi/96))
    Wait-For 'last tab X leaves This PC with a blank strip' { $window.Document.Folder.Self.Path -eq $pc -and [BandTest]::EmptyStrip($bar) } | Out-Null
    $drive=[IO.Path]::GetPathRoot($folder)
    $item=Wait-For 'system drive in This PC' { $window.Document.Folder.Items() | Where-Object { $_.Path -eq $drive } | Select-Object -First 1 }
    $window.Document.SelectItem($item,29)
    if (![BandTest]::EmptyStrip($bar)) { throw 'Selecting an item without opening it created a tab.' }
    Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
    $frame=[Windows.Automation.AutomationElement]::FromHandle($root)
    $selected=[Windows.Automation.PropertyCondition]::new([Windows.Automation.SelectionItemPattern]::IsSelectedProperty,$true)
    $type=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem)
    $entry=Wait-For 'selected drive item' {
        $frame.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.AndCondition]::new($selected,$type)) |
            Where-Object { $_.Current.Name.Contains($drive.TrimEnd('\')) } | Select-Object -First 1
    }
    $entry.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
    Wait-For 'opening the drive creates one selected directory tab' {
        $window.Document.Folder.Self.Path -eq $drive -and [BandTest]::Count($bar) -eq 1 -and [BandTest]::Index($bar) -eq 0 -and [BandTest]::IsWindowVisible($tabs)
    } | Out-Null
    [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor 12))
    Wait-For 'blank This PC ready for external open' { $window.Document.Folder.Self.Path -eq $pc -and [BandTest]::EmptyStrip($bar) } | Out-Null
    'PASS: This PC, Quick Access, per-tab close, switching and drag-out.'
    if (@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and [BandTest]::IsWindowVisible([FixtureWindow]::GetProp([IntPtr][long]$_.HWND,'EZFolder.Band.2')) }).Count) {
        'SKIP: another band is open and could receive the test merge.'
        return
    }
    Open-TestFolder $child
    Wait-For 'empty receiver gets one real tab for an external folder' { [BandTest]::Count($bar) -eq 1 -and [BandTest]::Index($bar) -eq 0 -and $window.Document.Folder.Self.Path -eq $child } | Out-Null
    Open-TestFolder $folder
    Wait-For 'external open becomes a second tab in the original HWND' {
        [BandTest]::Count($bar) -eq 2 -and $window.Document.Folder.Self.Path -eq $folder
    } | Out-Null
    Wait-For 'new window closes after the target receives its folder' {
        $extra = @($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $folder })
        $extra.Count -eq 0 -and [BandTest]::IsWindowEnabled($tabs)
    } | Out-Null
    [BandTest]::ClickTab($bar,12)
    Wait-For 'original tab still holds its folder' { $window.Document.Folder.Self.Path -eq $child } | Out-Null
    'PASS: external folder merged into a new tab, source window closed and original folder was preserved.'
    if ($MeasurePresentation) {
        Add-Type -Path (Join-Path $PSScriptRoot 'FramePresentation.cs')
        foreach ($number in 1..5) {
            $requested = Join-Path $folder "Presentation-$number"
            $measuredFolders += $requested
            [void][IO.Directory]::CreateDirectory($requested)
            $probe = New-Object FramePresentation
            try {
                Open-TestFolder $requested
                Wait-For 'measured open reaches destination tab' {
                    [BandTest]::Count($bar) -eq 3 -and $window.Document.Folder.Self.Path -eq $requested
                } | Out-Null
                Wait-For 'measured source window finishes closing' {
                    !@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $requested }).Count
                } | Out-Null
            } finally { $probe.Dispose() }
            'Sample {0}: frame {1:F0} ms, estimated visible {2:F0} ms, {3} visible / {4} cloaked samples' -f $number,$probe.FrameMs,$probe.VisibleMs,$probe.ComposedSamples,$probe.CloakedSamples
            if (!$probe.CloakedSamples) { throw 'No compositor suppression observed during this merge.' }
            [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor (([BandTest]::Index($bar)*[BandTest]::ItemWidth($tabs))+12)))
            Wait-For 'measured tab closed' { [BandTest]::Count($bar) -eq 2 } | Out-Null
            [BandTest]::ClickTab($bar,12)
            Wait-For 'restored original tab' { $window.Document.Folder.Self.Path -eq $child } | Out-Null
        }
    }
    foreach ($path in $burst) { [void][IO.Directory]::CreateDirectory($path); Open-TestFolder $path }
    Wait-For 'two more new windows become two tabs' { [BandTest]::Count($bar) -eq 4 } | Out-Null
    Wait-For 'both newly opened windows have handed off' {
        !@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -in $burst }).Count
    } | Out-Null
    Wait-For 'last merged tab is selected and ready to close' {
        [BandTest]::Index($bar) -eq 3 -and [BandTest]::IsWindowEnabled($tabs) -and $window.Document.Folder.Self.Path -in $burst
    } | Out-Null
    $visited = @($window.Document.Folder.Self.Path)
    [void][BandTest]::Send($tabs,0x208,0,((8 -shl 16) -bor (([BandTest]::Index($bar)*[BandTest]::ItemWidth($tabs))+12)))
    Wait-For 'other merged folder is still accessible' {
        [BandTest]::Count($bar) -eq 3 -and $window.Document.Folder.Self.Path -in $burst -and $window.Document.Folder.Self.Path -notin $visited
    } | Out-Null
    'PASS: two consecutive external opens kept both folders in separate tabs.'
    $window.ShowBrowserBar($id, $false, $null)
    Wait-For 'toolbar hidden' { ![BandTest]::Find($root).ToInt64() } | Out-Null
    $window.ShowBrowserBar($id, $true, $null)
    Wait-For 'toolbar visible again' { [BandTest]::Find($root).ToInt64() } | Out-Null
    'PASS: toolbar can be disabled and enabled again.'
} finally {
    foreach ($other in $testWindows) { try { $other.Quit() } catch {} }
    if ($window) { $window.Quit() }
    if ($null -eq $prior) { $settings.DeleteValue('NewTabLocation',$false) }
    else { $settings.SetValue('NewTabLocation',$prior,[Microsoft.Win32.RegistryValueKind]::DWord) }
    if ($null -ne $priorPreferences) { $settings.SetValue('PreferencesV1',$priorPreferences,[Microsoft.Win32.RegistryValueKind]::Binary) }
    $settings.Dispose()
    foreach ($path in ($burst + $measuredFolders)) { if (Test-Path -LiteralPath $path) { [IO.Directory]::Delete($path) } }
    if (Test-Path -LiteralPath $child) { [IO.Directory]::Delete($child) }
    if (Test-Path -LiteralPath $folder) { [IO.Directory]::Delete($folder) }
}
