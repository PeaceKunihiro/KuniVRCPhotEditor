@echo off
setlocal EnableDelayedExpansion
set "KUNI_TEST_CONFIG=Debug"
if /i "%~1"=="Release" set "KUNI_TEST_CONFIG=Release"
set "KUNI_TEST_VERSION="
for /f "tokens=3" %%v in ('findstr /b /c:"#define KUNI_VERSION_TEXT " "%~dp0Src\AppVersion.h"') do set "KUNI_TEST_VERSION=%%v"
if not defined KUNI_TEST_VERSION exit /b 1
set "KUNI_TEST_VERSION=%KUNI_TEST_VERSION:L=%"
set "KUNI_TEST_VERSION=%KUNI_TEST_VERSION:"=%"
set "KUNI_TEST_EXE=%~dp0x64\%KUNI_TEST_CONFIG%\KuniVRCPhotEditor_v%KUNI_TEST_VERSION%.exe"
"%KUNI_TEST_EXE%" --self-test
if not "%errorlevel%"=="0" exit /b 1
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\result.txt"
"%KUNI_TEST_EXE%" --ui-test
if not "%errorlevel%"=="0" exit /b 1
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\ui-result.txt"
for /l %%i in (1,1,20) do (
    "%KUNI_TEST_EXE%" --shutdown-test
    if not "!errorlevel!"=="0" exit /b 1
)
echo PASS: twenty immediate worker shutdowns
if /i not "%~2"=="Photos" exit /b 0
"%KUNI_TEST_EXE%" --photos-test
set "KUNI_PHOTOS_RESULT=%errorlevel%"
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\photos-result.txt"
exit /b %KUNI_PHOTOS_RESULT%
