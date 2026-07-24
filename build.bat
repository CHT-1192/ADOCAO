@echo off
setlocal enabledelayedexpansion

set "PORTABLE=OFF"
set "EXTRA_ZOOM=OFF"
set "INTERACTIVE=1"

rem -- CLI args --------------------------------------------------------
for %%a in (%*) do (
    set "INTERACTIVE=0"
    if /I "%%~a"=="portable"   set "PORTABLE=ON"
    if /I "%%~a"=="exzoom"     set "EXTRA_ZOOM=ON"
)

rem -- Compiler detection ----------------------------------------------
set "CC=" & set "CXX=" & set "COMPILER_KIND="
for /d %%d in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do (
    if exist "%%d\mingw64\bin\g++.exe" (
        set "CC=%%d\mingw64\bin\gcc.exe"
        set "CXX=%%d\mingw64\bin\g++.exe"
        set "COMPILER_KIND=G++ (MinGW)"
    )
)
if "%CXX%"=="" (
    where g++.exe >nul 2>&1 && (
        for /f "delims=" %%f in ('where g++.exe 2^>nul') do set "CXX=%%f"
        for /f "delims=" %%f in ('where gcc.exe 2^>nul')  do set "CC=%%f"
        set "COMPILER_KIND=G++ (PATH)"
    )
)
if "%CXX%"=="" (
    where clang++.exe >nul 2>&1 && (
        for /f "delims=" %%f in ('where clang++.exe 2^>nul') do set "CXX=%%f"
        for /f "delims=" %%f in ('where clang.exe 2^>nul')   do set "CC=%%f"
        set "COMPILER_KIND=Clang++"
    )
)
if "%CXX%"=="" (
    echo ERROR: No compiler found.
    exit /b 1
)

rem -- Banner ----------------------------------------------------------
echo(
echo ==============================================
echo   ADOCAO  v3.0.0  --  Build Script
echo ==============================================
echo(
echo Compiler detected: %COMPILER_KIND%
echo   CC  = %CC%
echo   CXX = %CXX%

rem -- Interactive prompts (skipped in CLI mode) -----------------------
if "%INTERACTIVE%"=="0" goto :summary
echo(
echo Build options (press Enter for default):
set /p "PORT_IN=  Static-linked portable build? (y/N): "
if /I "!PORT_IN!"=="y" set "PORTABLE=ON"
set /p "ZOOM_IN=  Extra zoom (min 0.5x)?           (y/N): "
if /I "!ZOOM_IN!"=="y" set "EXTRA_ZOOM=ON"

:summary
rem -- Summary ---------------------------------------------------------
echo(
echo Configuration:
echo   Portable:    %PORTABLE%
echo   Extra Zoom:  %EXTRA_ZOOM%
if defined GENERATOR (echo   Generator:   %GENERATOR%) else (echo   Generator:   MinGW Makefiles)
echo(

rem -- CMake configure -------------------------------------------------
if not exist build mkdir build
cd build

set "GEN=%GENERATOR%"
if "%GEN%"=="" set "GEN=MinGW Makefiles"

echo Configuring...
cmake .. -G "%GEN%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_C_COMPILER="%CC%" ^
    -DCMAKE_CXX_COMPILER="%CXX%" ^
    -DADOCAO_PORTABLE=%PORTABLE% ^
    -DADOCAO_EXTRA_ZOOM=%EXTRA_ZOOM%
if %ERRORLEVEL% NEQ 0 (echo CMake configure FAILED. & cd .. & exit /b 1)
echo   OK

rem -- CMake build -----------------------------------------------------
echo Building...
cmake --build . --parallel
if %ERRORLEVEL% NEQ 0 (taskkill /f /im adocao.exe >nul 2>&1 & cmake --build . --parallel)
cd ..

rem -- Done ------------------------------------------------------------
echo(
echo ==============================================
echo   Build successful!
echo ==============================================
echo(
echo   %cd%\build\ADOCAO.exe
echo(
endlocal
