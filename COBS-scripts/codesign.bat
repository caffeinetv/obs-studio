@echo off

rem Code sign EXEs, DLLs, and MSIs.
rem You can verify code signing signatures using the signtool verify command, but
rem be sure to pass the /pa flag to use the Default Authentication Verification Policy, like so:
rem   signtool verify /pa YOURFILE.EXE

setlocal

rem Set Developer environment for Visual Studio 2019
for /f "tokens=* USEBACKQ" %%f in (`call "%PROGRAMFILES(x86)%\Microsoft Visual Studio\Installer\vswhere" -version "[16.0,17.0)" -property installationPath`) DO (
    set VSINSTALLATION=%%f
)

if "%VSINSTALLATION%" == "" (
    echo Unable to find Visual Studio installation.
    exit /b 1
)

echo Found Visual Studio at %VSINSTALLATION%

rem Load Visual Studio environment variables
call "%VSINSTALLATION%\Common7\Tools\VsDevCmd.bat"
if %errorlevel% neq 0 (
    echo Error setting up Visual Studio environment"
    exit /b 1
)

signtool sign ^
 /i "DigiCert" ^
 /n "Caffeine" ^
 /tr "http://timestamp.digicert.com" ^
 /td sha256 ^
 /fd sha256 ^
 /a ^
 /du "https://caffeine.tv" ^
 /d "Caffeine" ^
 %*

if %errorlevel% neq 0 (
    echo Code signing failed. Verify DigiCert USB token inserted.
    exit /b 1
)

echo codesign.bat OK