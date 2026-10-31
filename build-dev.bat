@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build-dev mkdir build-dev
if not exist bin-dev\loader mkdir bin-dev\loader
cl /nologo /std:c++20 /utf-8 /O2 /MT /W4 /EHa /DUNICODE /DHHP_DEV /Fobuild-dev\ /LD src\main.cpp src\host.cpp src\progress.cpp src\notice.cpp src\dev_admin.cpp /link /MAP:build-dev\hollow-halls-progression.map /IMPLIB:build-dev\hollow-halls-progression.lib /OUT:bin-dev\loader\hollow-halls-progression.dll user32.lib kernel32.lib || exit /b 1
copy /y mod.json bin-dev\loader\mod.json >nul || exit /b 1
copy /y hollow-halls-progression.ini bin-dev\loader\hollow-halls-progression.ini >nul || exit /b 1
xcopy /y /i /q loading bin-dev\loader\loading >nul || exit /b 1
echo Built bin-dev\loader\hollow-halls-progression.dll (dev build: admin.enable on the server) for Greeneye Loader API 2
