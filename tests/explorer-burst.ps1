# Exercise real external opens against a single, private Explorer host.
param([switch]$SeparateProcess, [ValidateRange(2,10)][int]$Count = 8)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'FramePresentation.cs')
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class BurstNavigation : IDisposable {
    readonly Guid events=new Guid("34A715A0-6587-11D0-924A-0020AFC7AC4D");
    delegate void Completed(object dispatch,ref object url);
    readonly object window;
    readonly Completed callback;
    readonly HashSet<string> pending;
    public int Visits { get; private set; }
    public BurstNavigation(object window,string[] paths) {
        this.window=window;
        pending=new HashSet<string>(paths,StringComparer.OrdinalIgnoreCase);
        callback=OnNavigated;
        ComEventsHelper.Combine(window,events,252,callback);
    }
    void OnNavigated(object dispatch,ref object url) {
        Uri uri;
        if(Uri.TryCreate(Convert.ToString(url),UriKind.Absolute,out uri) && uri.IsFile && pending.Remove(uri.LocalPath)) ++Visits;
    }
    public void Dispose() { ComEventsHelper.Remove(window,events,252,callback); }
}
public static class BurstTabs {
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr w, int id);
    [DllImport("user32")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32")] static extern IntPtr SendMessageTimeout(IntPtr w,int msg,IntPtr a,IntPtr b,int flags,int ms,out IntPtr result);
    [DllImport("user32")] static extern bool GetClientRect(IntPtr w,out Rect r);
    [DllImport("user32")] static extern int GetDpiForWindow(IntPtr w);
    struct Rect { public int Left, Top, Right, Bottom; }
    public static int Send(IntPtr w,int msg,int a,int b) {
        IntPtr result;
        if (SendMessageTimeout(w,msg,(IntPtr)a,(IntPtr)b,2,2000,out result)==IntPtr.Zero) throw new Exception("Tab control timed out");
        return result.ToInt32();
    }
    public static void Click(IntPtr tabs,int index,int count) {
        Rect r; GetClientRect(tabs,out r);
        int dpi=GetDpiForWindow(tabs);
        int width=Math.Min((132*dpi+48)/96,(r.Right-(6*dpi+48)/96)/count);
        int point=(8<<16) | (index*width+10);
        Send(tabs,0x201,1,point);
        Send(tabs,0x202,0,point);
    }
}
'@
function Wait-Burst([string]$label,[scriptblock]$check) {
    $end = [datetime]::UtcNow.AddSeconds(20)
    do {
        if (& $check) { return }
        Start-Sleep -Milliseconds 40
    } while ([datetime]::UtcNow -lt $end)
    throw "Timed out waiting for $label"
}
$shell = New-Object -ComObject Shell.Application
foreach ($web in $shell.Windows()) {
    $other = [FixtureWindow]::GetProp([IntPtr][long]$web.HWND,'EZFolder.Band.2')
    if ([BurstTabs]::IsWindowVisible($other)) { throw 'Another visible band could receive the test folders.' }
}
$folder = Join-Path ([IO.Path]::GetTempPath()) ('EZFolder-burst-'+[guid]::NewGuid().ToString('N'))
$paths = @(1..$Count | ForEach-Object { Join-Path $folder "Burst-$_" })
$sameName=Join-Path $paths[0] ([IO.Path]::GetFileName($paths[1]))
$window = $null
try {
    foreach ($path in $paths) { [void][IO.Directory]::CreateDirectory($path) }
    $window = New-TestExplorer $folder -Separate:$SeparateProcess
    $root = [IntPtr][long]$window.HWND
    Wait-Burst 'fixture tab bar' {
        $bar = [FixtureWindow]::GetProp($root,'EZFolder.Band.2')
        $tabs = [BurstTabs]::GetDlgItem($bar,102)
        [BurstTabs]::IsWindowVisible($tabs) -and [BurstTabs]::Send($tabs,0x1304,0,0) -eq 1
    }
    $band = [FixtureWindow]::GetProp($root,'EZFolder.Band.2')
    $tabs = [BurstTabs]::GetDlgItem($band,102)
    $backgroundHandoff = [FixtureWindow]::GetProp($band,'EZFolder.Ready.3') -ne [IntPtr]::Zero
    "Host supports background handoff: $backgroundHandoff"
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $probe = New-Object FramePresentation($tabs)
    $navigation=[BurstNavigation]::new($window,[string[]]$paths)
    try {
        foreach ($path in $paths) {
            if ($SeparateProcess) {
                # Submit all launch requests directly. ShellExecute-based launching
                # can wait for Explorer between requests and turn this into serial opens.
                $start=[Diagnostics.ProcessStartInfo]::new("$env:SystemRoot\explorer.exe","/separate,`"$path`"")
                $start.UseShellExecute=$false
                [Diagnostics.Process]::Start($start).Dispose()
            }
            else { $shell.Explore($path) }
        }
        "Submitted $Count requests in $($clock.ElapsedMilliseconds) ms"
        Wait-Burst 'all distinct opens handed off and active' {
            [BurstTabs]::Send($tabs,0x1304,0,0) -eq $Count+1 -and $window.Document.Folder.Self.Path -in $paths -and
                !@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -in $paths }).Count
        }
        $clock.Stop()
        # Allow any deferred activation to finish before visiting tabs ourselves.
        Start-Sleep -Milliseconds 350
    } finally {
        $navigation.Dispose()
        $probe.Dispose()
        "Distinct requested folders with a completed host navigation: $($navigation.Visits)"
        "Opened $Count; tabs=$([BurstTabs]::Send($tabs,0x1304,0,0)); observed switches=$($probe.ObservedTabSwitches); estimated visible=$([int]$probe.VisibleMs) ms; visible samples before band=$($probe.BeforeBandSamples)/$($probe.ComposedSamples); cloaked samples=$($probe.CloakedSamples); elapsed=$($clock.ElapsedMilliseconds) ms"
    }
    $seen = @()
    foreach ($index in 0..$Count) {
        [BurstTabs]::Click($tabs,$index,($Count+1))
        Wait-Burst "tab $index" { [BurstTabs]::Send($tabs,0x130b,0,0) -eq $index -and $window.Document.Folder.Self.Path -notin $seen }
        $seen += $window.Document.Folder.Self.Path
    }
    if (Compare-Object (@($folder)+$paths) $seen) { throw 'Merged folder set differs from the folders opened.' }
    'PASS: each requested folder is accessible exactly once and the original tab is preserved.'
    $repeat=$seen[1]
    if ($SeparateProcess) { Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "/separate,`"$repeat`"" -WindowStyle Normal }
    else { $shell.ShellExecute($repeat) }
    Wait-Burst 'reopened folder selected at the right edge' {
        [BurstTabs]::Send($tabs,0x1304,0,0) -eq $Count+1 -and [BurstTabs]::Send($tabs,0x130b,0,0) -eq $Count -and
            $window.Document.Folder.Self.Path -eq $repeat -and !@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $repeat }).Count
    }
    $ordered=@($seen | Where-Object { $_ -ne $repeat })+@($repeat)
    for ($i=0;$i -lt $ordered.Count;$i++) {
        [BurstTabs]::Click($tabs,$i,$ordered.Count)
        Wait-Burst "reordered folder $i" { $window.Document.Folder.Self.Path -eq $ordered[$i] }
    }
    [void][IO.Directory]::CreateDirectory($sameName)
    $shell.Explore($sameName)
    Wait-Burst 'same basename in another directory opens a distinct tab' {
        [BurstTabs]::Send($tabs,0x1304,0,0) -eq $Count+2 -and $window.Document.Folder.Self.Path -eq $sameName
    }
    'PASS: reopening moves the existing tab to the end, preserves the other positions and distinguishes same-named folders.'
    if (!$navigation.Visits -or ($backgroundHandoff -and $navigation.Visits -ge $Count)) {
        throw 'Expected a completed host navigation and at least one background handoff during the burst.'
    }
} finally {
    if ($window) {
        $window.Quit()
        Wait-Burst 'fixture window closed' { !@($shell.Windows() | Where-Object { $_.HWND -eq $root.ToInt64() }).Count }
    }
    foreach ($web in @($shell.Windows())) {
        if ($web.Document.Folder.Self.Path -in (@($folder,$sameName)+$paths)) { $web.Quit() }
    }
    if ([IO.Directory]::Exists($sameName)) { [IO.Directory]::Delete($sameName) }
    foreach ($path in $paths) { if ([IO.Directory]::Exists($path)) { [IO.Directory]::Delete($path) } }
    if ([IO.Directory]::Exists($folder)) { [IO.Directory]::Delete($folder) }
}
