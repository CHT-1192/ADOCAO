@echo off
setlocal
set "PORTABLE=0"
if /I "%~1"=="portable" set "PORTABLE=1"

:: Find MinGW
set "MINGW64=%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64"
if not exist "%MINGW64%\bin\g++.exe" (
    for /d %%d in ("%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do set "MINGW64=%%d\mingw64"
)
if not exist "%MINGW64%\bin\g++.exe" (
    echo Cannot find MinGW. Install via: winget install BrechtSanders.WinLibs.POSIX.UCRT.LLVM
    exit /b 1
)
set "PATH=%MINGW64%\bin;%PATH%"
cd /d "%~dp0"

if not exist build mkdir build
cd build

if "%PORTABLE%"=="1" (
    echo === Building ADOCAO-Portable (static linked) ===
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DADOCAO_PORTABLE=ON -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe"
) else (
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DADOCAO_PORTABLE=OFF -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe"
)

cmake --build . --parallel
if %ERRORLEVEL% NEQ 0 (
    taskkill /f /im adocao.exe >nul 2>&1
    cmake --build . --parallel
)
echo.
if "%PORTABLE%"=="1" (
    echo Build done: %cd%\ADOCAO-Portable.exe
) else (
    echo Build done: %cd%\adocao.exe
)
endlocal
