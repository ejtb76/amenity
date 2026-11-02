@echo off
rem SPDX-License-Identifier: GPL-3.0-only
rem Build and run the break engine's host test with MSVC (renders WAVs, measures).
rem Needs build\jungle\amen-32k.bin: tools/jungle/make_amen_assets.sh (or its steps on Windows).
rem Output: build\jungle-host\*.wav
setlocal
set ROOT=%~dp0..\..
set OUT=%ROOT%\build\jungle-host
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%ROOT%\build\jungle\obj" mkdir "%ROOT%\build\jungle\obj"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /DJUNGLE_TRACE %CL_EXTRA% /FI"%~dp0host_shim.h" /I"%ROOT%\app\jungle" ^
    /Fe"%ROOT%\build\jungle\jungle_host.exe" /Fo"%ROOT%\build\jungle\obj\\" "%~dp0host_render.c" ^
    "%ROOT%\app\jungle\jungle.c" "%ROOT%\app\jungle\jungle_clock.c" "%ROOT%\app\jungle\sea_decode.c" || exit /b 1
"%ROOT%\build\jungle\jungle_host.exe" "%ROOT%\build\jungle\amen-32k.bin" "%OUT%"
