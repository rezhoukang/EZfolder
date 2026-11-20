# Run with 64-bit Windows PowerShell after installing the band.
param([switch]$RequireAlreadyVisible,[switch]$SeparateProcess,[switch]$MeasurePresentation)
$ErrorActionPreference = 'Stop'
$id = '{5129B68D-A44B-427B-AEE1-53137D3989EA}'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BandTest {
    delegate bool Visit(IntPtr w, IntPtr p);
    [DllImport("user32")] static extern bool EnumChildWindows(IntPtr w, Visit visit, IntPtr p);
    [DllImport("user32")] public static extern IntPtr GetDlgItem(IntPtr w, int id);
    [DllImport("user32")] static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32", SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr w, int m, IntPtr a, IntPtr b, int flags, int timeout, out IntPtr result);
    public static IntPtr Find(IntPtr root) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(root, (w,p) => {
            if (!IsWindowVisible(w) || GetDlgItem(w,100) == IntPtr.Zero || GetDlgItem(w,103) == IntPtr.Zero || GetDlgItem(w,102) == IntPtr.Zero) return true;
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
    public static void ClickTab(IntPtr bar, int x) {
        IntPtr tabs = GetDlgItem(bar,102);
        int point = (8 << 16) | x;
        Send(tabs,0x201,1,point);
        Send(tabs,0x202,0,point);
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
$prior = $settings.GetValue('MergeNewWindows')
function Open-TestFolder($path) {
    if ($SeparateProcess) { Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "/separate,`"$path`"" -WindowStyle Normal }
    else { $shell.Explore($path) }
}
try {
    $settings.SetValue('MergeNewWindows', 0, [Microsoft.Win32.RegistryValueKind]::DWord)
    Open-TestFolder $folder
    $window = Wait-For 'new Explorer test window' {
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
    [BandTest]::Send($bar,0x111,100,0) | Out-Null
    Wait-For 'second tab' { [BandTest]::Count($bar) -eq 2 -and [BandTest]::Index($bar) -eq 1 } | Out-Null
    $window.Navigate2($child)
    Wait-For 'child folder' { $window.Document.Folder.Self.Path -eq $child } | Out-Null
    [BandTest]::ClickTab($bar,12)
    Wait-For 'first tab restores original directory' { $window.Document.Folder.Self.Path -eq $folder -and [BandTest]::Index($bar) -eq 0 } | Out-Null
    [BandTest]::Send($bar,0x111,101,0) | Out-Null
    Wait-For 'closing first tab returns to child directory' { [BandTest]::Count($bar) -eq 1 -and $window.Document.Folder.Self.Path -eq $child } | Out-Null
    [BandTest]::Send($bar,0x111,101,0) | Out-Null
    if ([BandTest]::Count($bar) -ne 1) { throw 'Last tab was lost.' }
    'PASS: new, switch, close and keep last tab.'
    $button = [BandTest]::GetDlgItem($bar,103)
    if (!$button.ToInt64()) { throw 'This Explorer window has loaded the previous DLL.' }
    # Off: opening the parent creates a separate, usable window.
    Open-TestFolder $folder
    $independent = Wait-For 'separate Explorer window with merging off' {
        $shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $folder } | Select-Object -First 1
    }
    $testWindows += $independent
    Start-Sleep -Milliseconds 1200
    if ($independent.Document.Folder.Self.Path -ne $folder -or [BandTest]::Count($bar) -ne 1) { throw 'Merge-off was ignored.' }
    $independentId = $independent.HWND
    $independent.Quit()
    Wait-For 'independent test window has finished closing' {
        !@($shell.Windows() | Where-Object { $_.HWND -eq $independentId }).Count
    } | Out-Null
    $testWindows = @()
    'PASS: merge off leaves the second window independent.'
    # On: use the visible checkbox. The target is now the most recent Explorer window.
    [void][BandTest]::Send($button,0xF5,0,0)
    if ($settings.GetValue('MergeNewWindows') -ne 1) { throw 'Checkbox did not persist merge-on.' }
    Open-TestFolder $folder
    Wait-For 'external open becomes a second tab in the original HWND' {
        [BandTest]::Count($bar) -eq 2 -and $window.Document.Folder.Self.Path -eq $folder
    } | Out-Null
    Wait-For 'new window closes after the target receives its folder' {
        $extra = @($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $folder })
        $extra.Count -eq 0
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
            [void][BandTest]::Send($bar,0x111,101,0)
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
    $visited = @($window.Document.Folder.Self.Path)
    [void][BandTest]::Send($bar,0x111,101,0)
    Wait-For 'other merged folder is still accessible' {
        [BandTest]::Count($bar) -eq 3 -and $window.Document.Folder.Self.Path -in $burst -and $window.Document.Folder.Self.Path -notin $visited
    } | Out-Null
    'PASS: two consecutive external opens kept both folders in separate tabs.'
    [void][BandTest]::Send($button,0xF5,0,0)
    if ($settings.GetValue('MergeNewWindows') -ne 0) { throw 'Checkbox did not persist merge-off.' }
    $window.ShowBrowserBar($id, $false, $null)
    Wait-For 'toolbar hidden' { ![BandTest]::Find($root).ToInt64() } | Out-Null
    $window.ShowBrowserBar($id, $true, $null)
    Wait-For 'toolbar visible again' { [BandTest]::Find($root).ToInt64() } | Out-Null
    'PASS: toolbar can be disabled and enabled again.'
} finally {
    foreach ($other in $testWindows) { try { $other.Quit() } catch {} }
    foreach ($other in @($shell.Windows())) {
        try {
            if ($other.HWND -ne $window.HWND -and $other.Document.Folder.Self.Path -in (@($folder,$child) + $burst + $measuredFolders)) { $other.Quit() }
        } catch {}
    }
    if ($window) { $window.Quit() }
    if ($null -eq $prior) { $settings.DeleteValue('MergeNewWindows',$false) }
    else { $settings.SetValue('MergeNewWindows',$prior,[Microsoft.Win32.RegistryValueKind]::DWord) }
    $settings.Dispose()
    foreach ($path in ($burst + $measuredFolders)) { if (Test-Path -LiteralPath $path) { [IO.Directory]::Delete($path) } }
    if (Test-Path -LiteralPath $child) { [IO.Directory]::Delete($child) }
    if (Test-Path -LiteralPath $folder) { [IO.Directory]::Delete($folder) }
}
