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
    & $compiler -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static -municode -mwindows -s src/ribbon_entry.cpp -o build/EZFolderEntry.exe -lcomctl32 -lole32 -loleaut32 -luuid -lgdi32 -luser32 -ladvapi32
    if ($LASTEXITCODE) { throw "Ribbon entry build failed ($LASTEXITCODE)." }
    Get-Item build/EZFolder.dll,build/EZFolderEntry.exe
} finally { Pop-Location }
