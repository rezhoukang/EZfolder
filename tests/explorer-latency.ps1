# Measure the first observed foreground frame containing the destination item.
# Pass -External to compare ordinary Shell opens if no other band is visible.
# -PreparedViews uses a standalone IExplorerBrowser host to time rendering and reuse separately.
[CmdletBinding(DefaultParameterSetName='Explorer')]
param([ValidateRange(2,50)][int]$Samples=12,
      [Parameter(ParameterSetName='Explorer')][switch]$External,
      [Parameter(ParameterSetName='Prepared')][switch]$PreparedViews)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'fixture-window.ps1')
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing
Add-Type -Path (Join-Path $PSScriptRoot 'FolderPixels.cs') -ReferencedAssemblies System.Drawing

function Wait-Location($window,$path) {
    $until=[datetime]::UtcNow.AddSeconds(6)
    do {
        if ($window.Document.Folder.Self.Path -eq $path) { return }
        [Threading.Thread]::Sleep(10)
    } while ([datetime]::UtcNow -lt $until)
    throw "Explorer did not reach $path"
}
function Get-MarkerBounds($element,$name) {
    $condition=New-Object Windows.Automation.PropertyCondition([Windows.Automation.AutomationElement]::NameProperty,$name)
    $until=[datetime]::UtcNow.AddSeconds(5)
    do {
        $item=$element.FindFirst([Windows.Automation.TreeScope]::Descendants,$condition)
        if ($item -and !$item.Current.IsOffscreen) { return $item.Current.BoundingRectangle }
        [Threading.Thread]::Sleep(20)
    } while ([datetime]::UtcNow -lt $until)
    throw "Cannot locate calibration item $name"
}
function Measure-Route($route,$paths) {
    $results=@()
    foreach ($i in 0..($Samples-1)) {
        $index=($i+1)%2
        $probe.Start($index,$root)
        if ($route -eq 'prepared') { [FolderPixels]::Command($root,0x8001,$index) }
        elseif ($route -eq 'native') { [FolderPixels]::Command($root,0x8002,$i) }
        else {
            $p=if ($route -eq 'revisit') { $paths[$index] } else { $paths[$i] }
            if ($route -eq 'external') { $shell.Explore($p) } else { $window.Navigate2($p) }
        }
        try { $probe.Finish() }
        catch { throw "Measurement $route #$i (expected marker $index) stopped: $_" }
        if ($route -notin @('prepared','native')) { Wait-Location $window $p }
        $results += $probe.VisibleMs
        # Let the Shell finish focus/layout work before the next independent open.
        [Threading.Thread]::Sleep(100)
        if ($route -eq 'external') {
            $until=[datetime]::UtcNow.AddSeconds(6)
            while (@($shell.Windows() | Where-Object { $_.HWND -ne $window.HWND -and $_.Document.Folder.Self.Path -eq $p }).Count) {
                if ([datetime]::UtcNow -gt $until) { throw 'External window did not hand off.' }
                [Threading.Thread]::Sleep(20)
            }
        }
    }
    $sorted=@($results | Sort-Object)
    $median=($sorted[[int][math]::Floor(($Samples-1)/2)] + $sorted[[int][math]::Floor($Samples/2)])/2
    '{0}: n={1} min={2:F1} median={3:F1} p95={4:F1} max={5:F1} ms; <=50ms: {6}/{1}' -f $route,$Samples,$sorted[0],$median,$sorted[[math]::Ceiling(.95*$Samples)-1],$sorted[-1],@($sorted | Where-Object { $_ -le 50 }).Count
    '  samples (ms): '+(($results | ForEach-Object { '{0:F1}' -f $_ }) -join ', ')
}
$shell=New-Object -ComObject Shell.Application
if ($External) {
    foreach ($web in $shell.Windows()) {
        if ([FixtureWindow]::GetProp([IntPtr][long]$web.HWND,'EZFolder.Band.2') -ne [IntPtr]::Zero) {
            throw 'Another band could receive the external test. Run without -External for direct navigation only.'
        }
    }
}
$dir=Join-Path ([IO.Path]::GetTempPath()) ('EZFolder-latency-'+[guid]::NewGuid().ToString('N'))
$names=@('WWWWWWWWWWWW','IIIIIIIIIIII')
$known=@(); $fresh=@(); $outside=@(); $created=@()
$probe=$window=$fixture=$null
$exe=Join-Path $dir 'prepared-views.exe'
$context=[FolderPixels]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
    foreach ($route in @('known','fresh','outside','native')) {
        if ($route -eq 'outside' -and !$External) { continue }
        if (($route -eq 'native' -and !$PreparedViews) -or ($route -eq 'fresh' -and $PreparedViews)) { continue }
        $count=if ($route -eq 'known') { 2 } else { $Samples }
        for ($i=0;$i -lt $count;++$i) {
            $p=Join-Path $dir "$route-$i"
            $index=if ($route -eq 'known') { $i } else { ($i+1)%2 }
            $marker=Join-Path $p $names[$index]
            [void][IO.Directory]::CreateDirectory($marker)
            $created += $marker
            if ($route -eq 'known') { $known += $p } elseif ($route -eq 'fresh') { $fresh += $p } elseif ($route -eq 'outside') { $outside += $p }
        }
    }
    if ($PreparedViews) {
        & g++.exe -std=c++17 -O2 -Wall -Wextra -Werror -static -municode -mwindows (Join-Path $PSScriptRoot 'prepared-views.cpp') -o $exe -lshell32 -lole32 -luuid
        if ($LASTEXITCODE) { throw 'Could not compile the prepared-view fixture.' }
        $fixture=Start-Process $exe -ArgumentList ('"'+$dir+'"') -WindowStyle Normal -PassThru
        $until=[datetime]::UtcNow.AddSeconds(10)
        do {
            $fixture.Refresh()
            if ($fixture.HasExited) { throw 'Prepared-view fixture exited before showing its window.' }
            $root=$fixture.MainWindowHandle
            if ($root -ne [IntPtr]::Zero) { break }
            [Threading.Thread]::Sleep(20)
        } while ([datetime]::UtcNow -lt $until)
        if ($root -eq [IntPtr]::Zero) { throw 'Prepared-view fixture did not show its window.' }
    } else {
        $window=New-TestExplorer $dir -Separate
        $root=[IntPtr][long]$window.HWND
    }
    [FolderPixels]::FocusFixture($root)
    $element=[Windows.Automation.AutomationElement]::FromHandle($root)
    foreach ($index in @(1,0)) {
        if ($PreparedViews) {
            [FolderPixels]::Command($root,0x8001,$index)
        } else {
            $window.Navigate2($known[$index])
            Wait-Location $window $known[$index]
        }
        $bounds=Get-MarkerBounds $element $names[$index]
        [FolderPixels]::FocusFixture($root)
        [Threading.Thread]::Sleep(250)
        [void][FolderPixels]::DwmFlush()
        if (!$probe) {
            $rect=[Drawing.Rectangle]::FromLTRB([int]$bounds.Left,[int]$bounds.Top,[int][math]::Min($bounds.Right,$bounds.Left+390),[int]$bounds.Bottom)
            $probe=New-Object FolderPixels($rect)
        }
        $probe.Remember($index,$root)
    }
    $probe.Calibrate()
    if ($PreparedViews) {
        Measure-Route 'prepared' $null
        [FolderPixels]::Command($root,0x8001,0)
        [Threading.Thread]::Sleep(180)
        Measure-Route 'native' $null
    } else {
        foreach ($route in @('fresh','revisit','external')) {
            if ($route -eq 'external' -and !$External) { continue }
            $window.Navigate2($known[0])
            Wait-Location $window $known[0]
            [Threading.Thread]::Sleep(200)
            if ($route -eq 'fresh') { Measure-Route $route $fresh }
            elseif ($route -eq 'revisit') { Measure-Route $route $known }
            else { Measure-Route $route $outside }
        }
    }
} finally {
    if ($probe) { $probe.Dispose() }
    if ($window) { $window.Quit() }
    if ($fixture -and !$fixture.HasExited) {
        [void]$fixture.CloseMainWindow()
        if (!$fixture.WaitForExit(4000)) { $fixture.Kill(); $fixture.WaitForExit() }
    }
    if ([IO.File]::Exists($exe)) { [IO.File]::Delete($exe) }
    if ($External) {
        foreach ($web in $shell.Windows()) { if ($web.Document.Folder.Self.Path -in $outside) { $web.Quit() } }
    }
    [void][FolderPixels]::SetThreadDpiAwarenessContext($context)
    foreach ($marker in $created) { [IO.Directory]::Delete($marker); [IO.Directory]::Delete([IO.Path]::GetDirectoryName($marker)) }
    if ([IO.Directory]::Exists($dir)) { [IO.Directory]::Delete($dir) }
}
