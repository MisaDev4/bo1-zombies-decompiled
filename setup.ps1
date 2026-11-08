# setup.ps1 - point the built game at your Call of Duty: Black Ops install. Nothing of the install is copied (a few MB
# of DLLs aside) and the install is only ever read, never changed.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File setup.ps1 [-GameDir "D:\Games\Call of Duty Black Ops"]
#
# Without -GameDir the folder is taken from gamedir.txt (written by an earlier run), then looked up in Steam
# (registry + library folders) and the usual install paths.
#
# The game finds the install through +set fs_cdpath <install> (Play.bat, tools\mod-launcher.hta and tools\headless.ps1
# pass it; the path comes from gamedir.txt): main\*.iwd, zone\ (the fastfiles) and main\video (the movies) are read from
# there. Only what must sit next to the exe is put in build\Release: the DLLs, localization.txt and codlogo.bmp.
# To undo: delete gamedir.txt (and build\Release if you like).
param(
    [string]$GameDir = ''
)
$ErrorActionPreference = 'Stop'
$cfg = Join-Path $PSScriptRoot 'gamedir.txt'

function Test-BlackOpsDir([string]$dir) {
    $dir -and (Test-Path (Join-Path $dir 'zone\Common')) -and (Test-Path (Join-Path $dir 'main\iw_00.iwd'))
}

function Find-SteamLibraries {
    $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $steam) { $steam = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction SilentlyContinue).InstallPath }
    if (-not $steam) { return }
    $steam = $steam -replace '/', '\'
    $steam
    $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $m.Groups[1].Value -replace '\\\\', '\' }
    }
}

if (-not $GameDir -and (Test-Path $cfg)) { $GameDir = (Get-Content $cfg -TotalCount 1).Trim() }
if (-not $GameDir) {
    $candidates = @(Find-SteamLibraries | ForEach-Object { Join-Path $_ 'steamapps\common\Call of Duty Black Ops' }) +
        'C:\Program Files (x86)\Steam\steamapps\common\Call of Duty Black Ops'
    $GameDir = $candidates | Where-Object { Test-BlackOpsDir $_ } | Select-Object -First 1
}
if (-not (Test-BlackOpsDir $GameDir)) {
    throw "No Black Ops install found$(if ($GameDir) { " at '$GameDir'" }). Pass -GameDir with the folder that holds BlackOps.exe, zone\ and main\ (main\iw_00.iwd must exist)."
}
$GameDir = (Resolve-Path $GameDir).Path.TrimEnd('\')
"game install: $GameDir"
Set-Content -Path $cfg -Value $GameDir -Encoding ASCII

$dst = Join-Path $PSScriptRoot 'build\Release'
New-Item -ItemType Directory -Force (Join-Path $dst 'main') | Out-Null

# An earlier version of this script linked zone\ and main\video into build\Release with directory junctions. The game no
# longer needs them; remove the links (only links: a real folder is left alone, and the game still prefers it).
foreach ($j in 'zone', 'main\video') {
    $link = Join-Path $dst $j
    $item = Get-Item $link -Force -ErrorAction SilentlyContinue
    if ($item -and $item.LinkType -eq 'Junction') { [IO.Directory]::Delete($link); "removed old junction $link" }
}

# the engine's own DLLs (the exe is built against this Steam SDK and Bink)
Copy-Item (Join-Path $PSScriptRoot 'src\binklib\binkw32.dll') $dst -Force
Copy-Item (Join-Path $PSScriptRoot 'src\steam\redistributable_bin\*.dll') $dst -Force
# small loose files the exe reads from its working folder (localization.txt is required: the game asserts without it)
foreach ($f in 'localization.txt', 'codlogo.bmp', 'cod.bmp') {
    $src = Join-Path $GameDir $f
    if (Test-Path $src) { Copy-Item $src $dst -Force } elseif ($f -eq 'localization.txt') { throw "$src is missing" }
}
# runtimes the exe needs that a fresh Windows lacks, put next to the exe so nothing has to be installed:
# DirectX 9 (d3dx9_43, xinput1_3) out of the game's own Redist\DirectX, Visual C++ 2015-2022 (x86) out of Visual Studio
$dx = Join-Path $GameDir 'Redist\DirectX'
expand.exe (Join-Path $dx 'Jun2010_d3dx9_43_x86.cab') "-F:d3dx9_43.dll" $dst | Out-Null
expand.exe (Join-Path $dx 'APR2007_xinput_x86.cab') "-F:xinput1_3.dll" $dst | Out-Null
foreach ($f in 'd3dx9_43.dll', 'xinput1_3.dll') { if (-not (Test-Path (Join-Path $dst $f))) { throw "could not extract $f from $dx" } }
$crt = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\*\*\VC\Redist\MSVC\*\x86\Microsoft.VC14*.CRT' -Directory -ErrorAction SilentlyContinue |
    Sort-Object FullName | Select-Object -Last 1
if ($crt) { Copy-Item (Join-Path $crt.FullName 'msvcp140.dll'), (Join-Path $crt.FullName 'vcruntime140.dll') $dst -Force }
else { 'Visual C++ runtime not found next to Visual Studio: whoever plays needs the "Visual C++ Redistributable 2015-2022 (x86)"' }
"ready in $dst (game files are read from $GameDir; start with Play.bat)"
