param([Parameter(Mandatory)][ValidateSet('Enable','Disable')][string]$Action)
$ErrorActionPreference = 'Stop'
$id = '{5129B68D-A44B-427B-AEE1-53137D3989EA}'
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class FolderBandVisibility {
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern bool SetProp(IntPtr w, string key, IntPtr value);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr RemoveProp(IntPtr w, string key);
    [DllImport("user32", CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr w, string key);
}
'@
$shell = New-Object -ComObject Shell.Application
function Frames { @($shell.Windows() | Where-Object { $_.FullName -like '*\explorer.exe' -and $_.HWND }) }
$windows = @(Frames)
if (!$windows.Count) {
    $shell.Explore('shell:MyComputerFolder')
    for ($i = 0; $i -lt 50 -and !$windows.Count; ++$i) { Start-Sleep -Milliseconds 100; $windows = @(Frames) }
}
if (!$windows.Count) { throw 'Could not open an Explorer window.' }
foreach ($window in $windows) {
    $frame = [IntPtr][int64]$window.HWND
    if ($Action -eq 'Disable') {
        $window.ShowBrowserBar($id,$false,$null)
        continue
    }
    if (![FolderBandVisibility]::SetProp($frame,'EZFolder.KeepExistingFrame',[IntPtr]1)) { throw 'Could not mark existing Explorer frame.' }
    try {
        # Explorer may restore a "shown" flag without creating the band after a DLL upgrade.
        if (![FolderBandVisibility]::GetProp($frame,'EZFolder.Band.2').ToInt64()) {
            $window.ShowBrowserBar($id,$false,$null)
        }
        $window.ShowBrowserBar($id,$true,$null)
        for ($i = 0; $i -lt 30 -and ![FolderBandVisibility]::GetProp($frame,'EZFolder.Band.2').ToInt64(); ++$i) {
            Start-Sleep -Milliseconds 100
        }
        if (![FolderBandVisibility]::GetProp($frame,'EZFolder.Band.2').ToInt64()) { throw 'The new toolbar was not loaded.' }
    } finally { [void][FolderBandVisibility]::RemoveProp($frame,'EZFolder.KeepExistingFrame') }
}
