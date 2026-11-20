param([string]$PreviewPath)
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
    [DllImport("user32")] public static extern uint GetDpiForWindow(IntPtr w);
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    [DllImport("user32")] static extern IntPtr SendMessageTimeout(IntPtr w,int m,IntPtr a,IntPtr b,int flags,int ms,out IntPtr result);
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
$shell = New-Object -ComObject Shell.Application
$folder = Join-Path $env:TEMP ('EZFolder-button-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($folder)
$settings = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\EZFolder')
$previous = $settings.GetValue('MergeNewWindows')
$dpiContext = [RibbonEntryTest]::SetThreadDpiAwarenessContext([IntPtr](-4))
$window = $null
$initiallyShown = $null
try {
    $settings.SetValue('MergeNewWindows',0,[Microsoft.Win32.RegistryValueKind]::DWord)
    $shell.Explore($folder)
    $window = Wait-For 'temporary Explorer window' { $shell.Windows() | Where-Object { $_.Document.Folder.Self.Path -eq $folder } | Select-Object -First 1 }
    $root = [IntPtr][int64]$window.HWND
    $entry = [IntPtr](Wait-For 'ribbon entry' { [RibbonEntryTest]::Entry($root).ToInt64() })
    $button = [RibbonEntryTest]::GetDlgItem($entry,1)
    $initiallyShown = [RibbonEntryTest]::Shown($root)
    if ($initiallyShown) { [RibbonEntryTest]::Click($button); Wait-For 'initial hide' { ![RibbonEntryTest]::Shown($root) } | Out-Null }
    Wait-For 'click target in the ribbon while tabs are hidden' { Test-Placement } | Out-Null
    foreach ($wanted in @($true,$false,$true)) {
        [RibbonEntryTest]::Click($button)
        Wait-For "toggle to $wanted" { [RibbonEntryTest]::Shown($root) -eq $wanted } | Out-Null
        if (![RibbonEntryTest]::IsWindowVisible($button)) { throw 'Hiding the band removed the entry.' }
    }
    'PASS: hidden band can be enabled, disabled and re-enabled from the same button.'
    $width = $window.Width
    $window.Width = [math]::Max(480,$width-180)
    Wait-For 'right edge after resize' { $window.Width -ne $width -and (Test-Placement) } | Out-Null
    $window.Width = $width
    Wait-For 'right edge at original width' { Test-Placement } | Out-Null
    'PASS: the button follows the ribbon edge and receives hits at both widths.'
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
            [RibbonEntryTest]::Click($button)
            Wait-For 'restore initial display state' { [RibbonEntryTest]::Shown($root) -eq $initiallyShown } | Out-Null
        }
    } finally {
        if ($window) { $window.Quit() }
        if ($null -eq $previous) { $settings.DeleteValue('MergeNewWindows',$false) }
        else { $settings.SetValue('MergeNewWindows',$previous,[Microsoft.Win32.RegistryValueKind]::DWord) }
        $settings.Dispose()
        [void][RibbonEntryTest]::SetThreadDpiAwarenessContext($dpiContext)
        [IO.Directory]::Delete($folder)
    }
}
