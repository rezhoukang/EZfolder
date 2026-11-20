# Exercise real external opens against a single, private Explorer host.
param([switch]$SeparateProcess, [ValidateRange(2,10)][int]$Count = 8)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'FramePresentation.cs')
Add-Type @'
using System;
using System.Runtime.InteropServices;
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
    try {
        foreach ($path in $paths) {
            if ($SeparateProcess) { Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "/separate,`"$path`"" -WindowStyle Normal }
            else { $shell.Explore($path) }
        }
        Wait-Burst 'all distinct opens handed off and active' {
            [BurstTabs]::Send($tabs,0x1304,0,0) -eq $Count+1 -and $window.Document.Folder.Self.Path -in $paths -and
                !@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -in $paths }).Count
        }
        $clock.Stop()
        # Allow any deferred activation to finish before visiting tabs ourselves.
        Start-Sleep -Milliseconds 350
    } finally {
        $probe.Dispose()
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
    if ($backgroundHandoff -and $probe.ObservedTabSwitches -ge $Count) {
        throw 'Host navigated on every external open during the burst.'
    }
} finally {
    foreach ($web in @($shell.Windows())) {
        if ($web.Document.Folder.Self.Path -in (@($folder)+$paths)) { $web.Quit() }
    }
    foreach ($path in $paths) { if ([IO.Directory]::Exists($path)) { [IO.Directory]::Delete($path) } }
    if ([IO.Directory]::Exists($folder)) { [IO.Directory]::Delete($folder) }
}
