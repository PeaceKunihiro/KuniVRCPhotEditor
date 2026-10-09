@echo off
setlocal
set "KUNI_BUILD_CONFIG=Debug"
if /i "%~1"=="Release" set "KUNI_BUILD_CONFIG=Release"
"C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "%~dp0KuniVRCPhotEditor.sln" /p:Configuration=%KUNI_BUILD_CONFIG% /p:Platform=x64 /verbosity:minimal /nologo
if errorlevel 1 exit /b 1
if /i "%~2"=="Test" call "%~dp0verify.cmd" %KUNI_BUILD_CONFIG% %~3
exit /b %errorlevel%
