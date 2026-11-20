param([switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$directory = Join-Path $env:ProgramW6432 'EZFolder'
$startup = [Environment]::GetFolderPath('Startup')
$shortcut = Join-Path $startup 'EZFolder Ribbon Entry.lnk'
$session = (Get-Process -Id $PID).SessionId
$prefix = 'EZFolderEntry-'
$installedName = '^EZFolderEntry-[a-f0-9]{64}\.exe$'
$target = $null
if (!$Uninstall) {
    $source = (Get-Item -LiteralPath (Join-Path $PSScriptRoot 'build\EZFolderEntry.exe')).FullName
    $hash = (Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant()
    [void][IO.Directory]::CreateDirectory($directory)
    $target = Join-Path $directory "$prefix$hash.exe"
    if (!(Test-Path -LiteralPath $target)) { [IO.File]::Copy($source,$target) }
    if ((Get-FileHash -LiteralPath $target).Hash -ne $hash) { throw 'Ribbon entry checksum mismatch.' }
}
$running = @(Get-Process -Name 'EZFolderEntry-*' -ErrorAction SilentlyContinue | Where-Object {
    $_.SessionId -eq $session -and $_.Path -and [IO.Path]::GetDirectoryName($_.Path) -eq $directory -and
    [IO.Path]::GetFileName($_.Path) -match $installedName
})
foreach ($process in $running) {
    if ($process.Path -eq $target) { continue }
    Start-Process -FilePath $process.Path -ArgumentList '/stop' -WindowStyle Hidden -Wait
    if (!$process.WaitForExit(5000)) {
        # The entry has no session data. Explorer owns the folder windows and tabs.
        $process.Kill()
        if (!$process.WaitForExit(5000)) { throw 'The previous ribbon entry is still running.' }
    }
}
if ($Uninstall) {
    if (Test-Path -LiteralPath $shortcut) { Remove-Item -LiteralPath $shortcut }
} else {
    [void][IO.Directory]::CreateDirectory($startup)
    $link = (New-Object -ComObject WScript.Shell).CreateShortcut($shortcut)
    $link.TargetPath = $target
    $link.WorkingDirectory = $directory
    $link.Description = 'EZFolder ribbon button'
    $link.Save()
    if (!@($running | Where-Object { !$_.HasExited -and $_.Path -eq $target }).Count) {
        # Launch in the interactive Explorer session. The button needs normal user privileges.
        $shell = New-Object -ComObject Shell.Application
        $frame = $shell.Windows() | Where-Object { $_.FullName -like '*\explorer.exe' -and $_.HWND } | Select-Object -First 1
        if ($frame) { $frame.Document.Application.ShellExecute($target,'',$directory,'open',0) }
        else { Start-Process "$env:SystemRoot\explorer.exe" -ArgumentList "`"$shortcut`"" -WindowStyle Hidden }
    }
    Write-Output "Ribbon entry installed: $target"
}
if (Test-Path -LiteralPath $directory) {
    Get-ChildItem -LiteralPath $directory -File | Where-Object { $_.Name -match $installedName -and $_.FullName -ne $target } | ForEach-Object {
        try { Remove-Item -LiteralPath $_.FullName -ErrorAction Stop }
        catch { Write-Warning "The ribbon entry file is still in use: $($_.Name)" }
    }
}
