# Create a private Explorer automation frame before navigating to the fixture.
function New-TestExplorer([string]$path,[switch]$Separate) {
    if ($Separate) {
        $marker = Join-Path $path ('fixture-'+[guid]::NewGuid().ToString('N'))
        [IO.File]::WriteAllText($marker,'')
        $shell = New-Object -ComObject Shell.Application
        $existing = @($shell.Windows() | ForEach-Object { $_.HWND })
        try {
            Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "/separate,/select,`"$marker`"" -WindowStyle Normal
            for ($i=0;$i -lt 100;$i++) {
                $web = $shell.Windows() | Where-Object { $_.HWND -notin $existing -and $_.Document.Folder.Self.Path -eq $path } | Select-Object -First 1
                if ($web -and [FixtureWindow]::SetProp([IntPtr][long]$web.HWND,'EZFolder.KeepExistingFrame',[IntPtr]1)) { return $web }
                Start-Sleep -Milliseconds 80
            }
            throw 'Separate Explorer fixture did not open.'
        } finally { [IO.File]::Delete($marker) }
    }
    $web = [Activator]::CreateInstance([type]::GetTypeFromCLSID([guid]'C08AFD90-F2A1-11D1-8455-00A0C91F3880'))
    try {
        if (![FixtureWindow]::SetProp([IntPtr][long]$web.HWND,'EZFolder.KeepExistingFrame',[IntPtr]1)) {
            throw 'Cannot reserve test frame.'
        }
        $web.Navigate2($path)
        $web.Visible = $true
        return $web
    } catch { $web.Quit(); throw }
}
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class FixtureWindow {
    [DllImport("user32",CharSet=CharSet.Unicode)] public static extern bool SetProp(IntPtr w,string key,IntPtr value);
    [DllImport("user32",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr w,string key);
}
"@
