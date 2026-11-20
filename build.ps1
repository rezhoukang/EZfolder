$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $compiler = (Get-Command g++.exe -ErrorAction Stop).Source
    if ((& $compiler -dumpmachine) -notmatch '^x86_64-.*mingw') {
        throw 'Use the 64-bit MinGW-w64 compiler.'
    }
    New-Item -ItemType Directory -Force -Path build | Out-Null
    & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -shared -s src/deskband.cpp -o build/EZFolder.dll -lcomctl32 -luxtheme -ldwmapi -lshell32 -lole32 -loleaut32 -luuid -lgdi32 -luser32 -ladvapi32
    if ($LASTEXITCODE) { throw "Build failed ($LASTEXITCODE)." }
    $framework = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319'
    $metadata = Join-Path $env:SystemRoot 'System32\WinMetadata'
    $refs = foreach ($name in 'Windows.Foundation','Windows.UI','Windows.UI.Xaml') {
        '/reference:' + (Join-Path $metadata "$name.winmd")
    }
    $refs += foreach ($name in 'System.Runtime','System.Runtime.InteropServices.WindowsRuntime') {
        '/reference:' + (Get-ChildItem "$env:SystemRoot\Microsoft.NET\assembly\GAC_MSIL\$name\*\$name.dll" | Select-Object -First 1).FullName
    }
    & "$framework\csc.exe" /nologo /target:winexe /platform:x64 /optimize+ /utf8output /codepage:65001 /out:build\EZFolderSettings.exe /win32manifest:src\settings.manifest /resource:src\settings.xaml,settings.xaml @refs /reference:System.Drawing.dll,System.Windows.Forms.dll,System.Runtime.WindowsRuntime.dll,System.Runtime.WindowsRuntime.UI.Xaml.dll src\SettingsWindow.cs
    if ($LASTEXITCODE) { throw "Windows XAML settings build failed ($LASTEXITCODE)." }
    & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -municode -mwindows -s src/ribbon_entry.cpp -o build/EZFolderEntry.exe -lcomctl32 -luxtheme -lole32 -loleaut32 -luuid -lgdi32 -luser32 -ladvapi32
    if ($LASTEXITCODE) { throw "Ribbon entry build failed ($LASTEXITCODE)." }
    Get-Item build/EZFolder.dll,build/EZFolderEntry.exe,build/EZFolderSettings.exe
} finally { Pop-Location }
