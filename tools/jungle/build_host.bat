@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
rem Build the sample bank (make_amen_assets.bat) and run the JUNGLE host
rem test with MSVC. Needs the Amen sample: it renders the break.
rem Usage: build_host.bat [amen.wav]
rem Output: build\jungle\JUNGLE-AMEN.bin, build\jungle\amen_bank.inc and build\jungle-host\*.wav
setlocal
set ROOT=%~dp0..\..
set OUT=%ROOT%\build\jungle-host
set BANK=%ROOT%\build\jungle\JUNGLE-AMEN.bin
if not exist "%OUT%" mkdir "%OUT%"
call "%~dp0make_amen_assets.bat" %1 || exit /b 1
if not exist "%BANK%" (echo The host test needs the Amen sample. & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
set INC=/I"%ROOT%\sdk\include" /I"%ROOT%\web\synth"
cl /nologo /O2 /W3 /DFM1_HOST_TEST /DJUNGLE_TRACE %INC% /Fe"%ROOT%\build\jungle\jungle_host.exe" /Fo"%ROOT%\build\jungle\obj\\" ^
    "%~dp0host_render.c" "%ROOT%\app\jungle\jungle.c" "%ROOT%\web\synth\sea_decode.c" >nul || exit /b 1
"%ROOT%\build\jungle\jungle_host.exe" "%BANK%" "%OUT%"
