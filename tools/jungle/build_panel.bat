@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
rem Build and run the JUNGLE panel host test (app/screen.c with stubs).
rem Needs build\jungle\JUNGLE-AMEN.bin from build_host.bat.
rem Output: build\jungle-panel\*.png and panel-session.wav
setlocal
set ROOT=%~dp0..\..
set OUT=%ROOT%\build\jungle-panel
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%ROOT%\build\jungle\obj-panel" mkdir "%ROOT%\build\jungle\obj-panel"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
rem GCC attributes mean nothing to MSVC on the host.
cl /nologo /O2 /W3 /std:c11 /DFM1_HOST_TEST /D_CRT_SECURE_NO_WARNINGS /FI"%~dp0host_shim.h" ^
    /I"%ROOT%\sdk\include" /I"%ROOT%\app" /I"%ROOT%\app\synth" /I"%ROOT%\web\synth" ^
    /Fe"%ROOT%\build\jungle\jungle_panel.exe" /Fo"%ROOT%\build\jungle\obj-panel\\" ^
    "%~dp0host_panel.c" "%ROOT%\app\screen.c" "%ROOT%\app\jungle\jungle.c" "%ROOT%\web\synth\sea_decode.c" || exit /b 1
"%ROOT%\build\jungle\jungle_panel.exe" "%ROOT%\build\jungle\JUNGLE-AMEN.bin" "%OUT%" || exit /b 1
"%ROOT%\build\jungle\jungle_panel.exe" "%ROOT%\build\jungle\JUNGLE-AMEN.bin" "%OUT%" nobank || exit /b 1
python -I "%~dp0panel_png.py" "%OUT%"
