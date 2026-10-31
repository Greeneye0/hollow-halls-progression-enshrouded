@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist build mkdir build
if not exist bin\loader mkdir bin\loader
cl /nologo /std:c++20 /utf-8 /O2 /MT /W4 /EHa /DUNICODE /Fobuild\ /LD src\main.cpp src\host.cpp src\progress.cpp src\notice.cpp src\dev_admin.cpp /link /MAP:build\hollow-halls-progression.map /IMPLIB:build\hollow-halls-progression.lib /OUT:bin\loader\hollow-halls-progression.dll user32.lib kernel32.lib || exit /b 1
copy /y mod.json bin\loader\mod.json >nul || exit /b 1
copy /y hollow-halls-progression.ini bin\loader\hollow-halls-progression.ini >nul || exit /b 1
xcopy /y /i /q loading bin\loader\loading >nul || exit /b 1
echo Built bin\loader\hollow-halls-progression.dll for Greeneye Loader API 2
