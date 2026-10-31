@echo off
rem SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
rem Build AMENITY: sample-bank files (if the Amen sample is there), firmware
rem and installer page, copied to build\install\AMENITY-<version>-installer.html.
rem Usage: build_release.bat <https source archive URL>
rem   set AMENITY_WITH_AMEN=0 first to build without the sample.
setlocal
set ROOT=%~dp0..\..
if "%~1"=="" (echo Usage: build_release.bat ^<https source archive URL^> & exit /b 2)
call "%~dp0make_amen_assets.bat" || exit /b 1
set FM1_SOURCE_URL=%~1
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\sdk\Build.ps1" -Package -Toolchain C:\JL\pi32 || exit /b 1
for /f "tokens=3 delims= " %%v in ('findstr /c:"FM1_RELEASE_VERSION" "%ROOT%\app\version.h"') do set VERSION=%%~v
if not exist "%ROOT%\build\install" mkdir "%ROOT%\build\install"
copy /y "%ROOT%\build\official-web\FM1-Official-Open-Source.html" "%ROOT%\build\install\AMENITY-%VERSION%-installer.html" >nul || exit /b 1
copy /y "%ROOT%\build\dx7\generated\FM1_OFFICIAL_OPEN.fwsc" "%ROOT%\build\install\AMENITY-%VERSION%.fwsc" >nul || exit /b 1
echo Installer: build\install\AMENITY-%VERSION%-installer.html
