@echo off

setlocal
set ERRORLEVEL=0
set NSIS_PATH=C:\Program Files (x86)\NSIS
set APPVERSION=25.0.8
goto :MAIN

:: @function to check environment variables/dependencies for this project
:-check
    echo Checking required dependencies... 
    if not defined QTDIR goto :error 
    if not defined obsInstallerTempDir goto :error 
    if not defined DepsPath goto :error 
    if not defined LIBCAFFEINE_DIR goto :error 
    if not defined CEF_ROOT_DIR goto :error
    echo Successfully found all the dependencies.
    goto:EOF


:: @function to clean the build directories
:clean
    IF EXIST %~1 (
    del /q /f %~1\* && for /d %%x in (%~1\*) do @rd /s /q "%%x"
    ) ELSE (
    mkdir %~1
    )
    goto:EOF


:: @function to build 64 bit version of COBS
:-build 
    call :-check
    if ERRORLEVEL 1 goto:EOF
    call :clean "..\build64"
    echo Building 64 bit version 
    cd ..
    cmake -H. -Bbuild64 -G "Visual Studio 16 2019" -A"x64" -T"host=x64" -DCOPIED_DEPENDENCIES=OFF -DENABLE_SCRIPTING=ON -DENABLE_UI=ON -DCOMPILE_D3D12_HOOK=ON -DDepsPath="%DepsPath%" -DQTDIR="%QTDIR%" -DLIBCAFFEINE_DIR="%LIBCAFFEINE_DIR%" -DCEF_ROOT_DIR="%CEF_ROOT_DIR%" -DBUILD_BROWSER=ON 
    cmake --build build64 --config RelWithDebInfo
    echo Built 64 bit COBS
    cd COBS-scripts
    goto:EOF


:: @function to create package COBS
:-package
    call :-check 
    echo File %NSIS_PATH%
    if not exist "%NSIS_PATH%\Plugins\x86-unicode\OBSInstallerUtils.dll" (
        echo Prerequisite to for packaging not found.
        echo Please copy "install-win\AdditionalDLLs\OBSInstallerUtils.dll" file into "%NSIS_PATH%\Plugins\x86-unicode"
        goto:EOF
    ) 
    "%NSIS_PATH%\makensis.exe" install-win\cobs-installer.nsi
    echo Packaging complete. 
    echo Signing the package.....
    if not exist "install-win\OBS-Studio-Caffeine-25.0.8-Installer-x64.exe" (
        goto:EOF
    )


:: @Function to sign the installer
:-sign
    echo Signing the package.....
    if not exist "install-win\OBS-Studio-Caffeine-%APPVERSION%-Installer-x64.exe" (
        goto:EOF
    )
    call codesign.bat "install-win\OBS-Studio-Caffeine-%APPVERSION%-Installer-x64.exe"
    if ERRORLEVEL 1 (
        echo Failed to sign installer.
        goto:EOF
    )
    goto:EOF


:: @function to print the supported options 
:-help 
    echo Supported options are:
    echo -help : Print this output.
    echo -check : Checks environment variables/ dependencies for this project.
    echo -build : Builds 64 bit version of obs. 
    echo -package : Builds package.
    echo -sign : Signs the package.
    goto:EOF


:: @function for error handling
:error
    echo Error missing dependencies.Check if environment variables are set for QTDIR, obsInstallerTempDir, DepsPath, LIBCAFFEINE_DIR or CEF_ROOT_DIR.
    EXIT /b 1
    goto:EOF


:: @function entry point for the script
:MAIN
    if [%1]==[] (
        echo Please choose from supported options:
        call:-help
        goto:EOF
    )
    call :%~1
    goto:EOF
