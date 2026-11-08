@echo off
rem Black Ops 1 Zombies: the original game. Opens at its own main menu: Zombies, Solo, pick a map. No mods.
rem (Mods: tools\mod-launcher.hta, see README.md.)
rem "Play.bat profile" runs the Profile build (BO1Zombies_profile.exe) instead: connect the Tracy 0.13.x viewer to it.
set "GAME=%~dp0build\Release"
set "EXE=BO1Zombies.exe"
if /i "%~1"=="profile" set "EXE=BO1Zombies_profile.exe"
if not exist "%GAME%\%EXE%" (echo Game not found: %GAME%\%EXE% & pause & exit /b 1)
if not exist "%~dp0gamedir.txt" (echo Run setup.ps1 first: it records where your Black Ops install is. & pause & exit /b 1)
set /p BO1_INSTALL=<"%~dp0gamedir.txt"
cd /d "%GAME%"
start "" "%GAME%\%EXE%" +set bo1_zombies 1 +set fs_b "%GAME%" +set fs_h "%GAME%" +set fs_cdpath "%BO1_INSTALL%" +set logfile 2 +set net_ip 127.0.0.1 +set r_fullscreen 0 +set r_borderless 1 +set vid_xpos 0 +set vid_ypos 0
