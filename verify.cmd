@echo off
setlocal
set "KUNI_TEST_CONFIG=Debug"
if /i "%~1"=="Release" set "KUNI_TEST_CONFIG=Release"
set "KUNI_TEST_EXE=%~dp0x64\%KUNI_TEST_CONFIG%\KuniVRCPhotEditor.exe"
"%KUNI_TEST_EXE%" --self-test
if not "%errorlevel%"=="0" exit /b 1
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\result.txt"
"%KUNI_TEST_EXE%" --ui-test
if not "%errorlevel%"=="0" exit /b 1
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\ui-result.txt"
if /i not "%~2"=="Photos" exit /b 0
"%KUNI_TEST_EXE%" --photos-test
set "KUNI_PHOTOS_RESULT=%errorlevel%"
type "%~dp0x64\%KUNI_TEST_CONFIG%\self-test-data\photos-result.txt"
exit /b %KUNI_PHOTOS_RESULT%
