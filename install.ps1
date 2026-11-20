param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$id = '{5129B68D-A44B-427B-AEE1-53137D3989EA}'
$legacyId = '{6751F5D7-8C60-4B05-9A79-6EB61FABED30}'
$class = "Software\Classes\CLSID\$id"
$toolbar = 'Software\Microsoft\Internet Explorer\Toolbar'
# CATID_CommBand is the horizontal Explorer band category.
$category = '{00021494-0000-0000-C000-000000000046}'
$identity = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this installer from an administrator PowerShell window.'
}
$registry = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64')
$installDir = Join-Path ([Environment]::GetEnvironmentVariable('ProgramW6432')) 'EZFolder'
$menu = Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'EZFolder'
$labels = @{ Enable = '开启 EZFolder'; Disable = '关闭 EZFolder' }
try {
    if ($Uninstall) {
        & (Join-Path $PSScriptRoot 'setup-entry.ps1') -Uninstall
        $key = $registry.OpenSubKey($toolbar, $true)
        if ($key) { try { $key.DeleteValue($id, $false) } finally { $key.Dispose() } }
        $registry.DeleteSubKeyTree($class, $false)
        foreach ($label in $labels.Values) {
            $shortcut = Join-Path $menu "$label.lnk"
            if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut }
        }
        if ((Test-Path -LiteralPath $menu) -and !(Get-ChildItem -Force -LiteralPath $menu)) { [IO.Directory]::Delete($menu) }
        if (Test-Path -LiteralPath $installDir) {
            $controller = Join-Path $installDir 'visibility.ps1'
            if (Test-Path -LiteralPath $controller) { Remove-Item -LiteralPath $controller }
            Get-ChildItem -LiteralPath $installDir -File | Where-Object Name -match '^EZFolder-[a-f0-9]{64}\.dll$' | ForEach-Object {
                try { Remove-Item -LiteralPath $_.FullName -ErrorAction Stop }
                catch { Write-Warning "Close Explorer windows and run uninstall again to remove $($_.FullName)." }
            }
            if (!(Get-ChildItem -Force -LiteralPath $installDir)) { [IO.Directory]::Delete($installDir) }
        }
        Write-Output 'EZFolder unregistered.'
    } else {
        $source = (Get-Item -LiteralPath (Join-Path $PSScriptRoot 'build\EZFolder.dll')).FullName
        $hash = (Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant()
        [IO.Directory]::CreateDirectory($installDir) | Out-Null
        $dll = Join-Path $installDir "EZFolder-$hash.dll"
        if (!(Test-Path -LiteralPath $dll)) { [IO.File]::Copy($source, $dll) }
        if ((Get-FileHash -LiteralPath $dll).Hash -ne $hash) { throw 'Installed DLL checksum mismatch.' }
        $key = $registry.CreateSubKey($class)
        try {
            $key.SetValue('', 'EZFolder')
            $key.SetValue('MenuText', 'EZFolder')
            $key.SetValue('HelpText', 'EZFolder folder tabs')
        } finally { $key.Dispose() }
        $key = $registry.CreateSubKey("$class\InprocServer32")
        try {
            $key.SetValue('', $dll)
            $key.SetValue('ThreadingModel', 'Apartment')
        } finally { $key.Dispose() }
        $registry.CreateSubKey("$class\Implemented Categories\$category").Dispose()
        $key = $registry.CreateSubKey($toolbar)
        try { $key.SetValue($id, 'EZFolder') } finally { $key.Dispose() }
        $controller = Join-Path $installDir 'visibility.ps1'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'visibility.ps1') -Destination $controller -Force
        [IO.Directory]::CreateDirectory($menu) | Out-Null
        $wsh = New-Object -ComObject WScript.Shell
        foreach ($action in $labels.Keys) {
            $link = $wsh.CreateShortcut((Join-Path $menu ($labels[$action] + '.lnk')))
            $link.TargetPath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
            $link.Arguments = "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File `"$controller`" -Action $action"
            $link.WorkingDirectory = $installDir
            $link.Save()
        }
        & (Join-Path $PSScriptRoot 'setup-entry.ps1')
        Write-Output "EZFolder registered: $dll"
    }
    $oldClass = "Software\Classes\CLSID\$legacyId"
    $oldServer = $registry.OpenSubKey("$oldClass\InprocServer32")
    if ($oldServer) {
        try { $oldPath = $oldServer.GetValue('') } finally { $oldServer.Dispose() }
        if ($oldPath -and [IO.Path]::GetDirectoryName($oldPath) -eq $installDir -and [IO.Path]::GetFileName($oldPath) -match '^EZFolder-[a-f0-9]{64}\.dll$') {
            $key = $registry.OpenSubKey($toolbar,$true)
            if ($key) { try { $key.DeleteValue($legacyId,$false) } finally { $key.Dispose() } }
            $registry.DeleteSubKeyTree($oldClass,$false)
        }
    }
} finally { $registry.Dispose() }
