@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
python tools\gen_proxy.py src || exit /b 1
python tools\check_escapes.py src | findstr /c:"bad escapes: 0" >nul || (python tools\check_escapes.py src & exit /b 1)
ml64 /nologo /c /Fo build\proxy_stubs.obj src\proxy_stubs.asm || exit /b 1
cl /nologo /O2 /MT /EHsc /W3 /std:c++17 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /c src\*.cpp || exit /b 1
link /nologo /DLL /OUT:build\winmm.dll /DEF:src\winmm.def build\*.obj user32.lib kernel32.lib psapi.lib advapi32.lib gdi32.lib || exit /b 1
echo BUILD OK
