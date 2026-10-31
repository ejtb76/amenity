@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
rem Turn the third-party Amen break into the build's sample files:
rem   build\jungle\amen-32k.raw       32 kHz mono 16-bit
rem   build\jungle\JUNGLE-AMEN.bin    FM-1 sample bank (slot 1 "AMEN 160")
rem   build\jungle\amen_bank.inc      that bank as a C array, for the installer page
rem   build\jungle\amen_fallback.inc  a 20 kHz copy, built into the firmware and
rem                                   played while the bank holds no AMEN slot
rem All of them are copies of third-party audio and stay in build\ (ignored).
rem Without assets\third-party\amen\amen.wav, or with AMENITY_WITH_AMEN=0,
rem nothing is made and the build carries no sample (the firmware then shows
rem NO BREAK until a bank with an AMEN slot is written).
rem Usage: make_amen_assets.bat [amen.wav]
setlocal
set ROOT=%~dp0..\..
set WAV=%~1
if "%WAV%"=="" set WAV=%ROOT%\assets\third-party\amen\amen.wav
if not exist "%ROOT%\build\jungle\obj" mkdir "%ROOT%\build\jungle\obj"
if "%AMENITY_WITH_AMEN%"=="0" goto without
if not exist "%WAV%" goto without
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cl /nologo /O2 /W3 /I"%ROOT%\sdk\include" /I"%ROOT%\web\synth" /Fe"%ROOT%\build\jungle\make_bank.exe" ^
    /Fo"%ROOT%\build\jungle\obj\\" "%~dp0make_bank.c" "%ROOT%\web\synth\sea_decode.c" >nul || exit /b 1
python -I "%~dp0make_amen.py" "%WAV%" "%ROOT%\build\jungle\amen-32k.raw" 32000 || exit /b 1
"%ROOT%\build\jungle\make_bank.exe" "%ROOT%\build\jungle\amen-32k.raw" "%ROOT%\build\jungle\JUNGLE-AMEN.bin" "AMEN 160" || exit /b 1
python -I "%~dp0bank_header.py" "%ROOT%\build\jungle\JUNGLE-AMEN.bin" "%ROOT%\build\jungle\amen_bank.inc" || exit /b 1
python -I "%~dp0make_amen.py" "%WAV%" "%ROOT%\build\jungle\amen-20k.raw" 20000 || exit /b 1
"%ROOT%\build\jungle\make_bank.exe" "%ROOT%\build\jungle\amen-20k.raw" "%ROOT%\build\jungle\amen-20k.bin" "AMEN 160" || exit /b 1
python -I "%~dp0bank_header.py" "%ROOT%\build\jungle\amen-20k.bin" "%ROOT%\build\jungle\amen_fallback.inc" --fallback || exit /b 1
exit /b 0
:without
echo Building without the Amen sample.
for %%f in (amen_bank.inc amen_fallback.inc JUNGLE-AMEN.bin) do if exist "%ROOT%\build\jungle\%%f" del "%ROOT%\build\jungle\%%f"
exit /b 0
