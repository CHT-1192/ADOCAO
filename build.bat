@echo off
set "MINGW64=%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64"
if not exist "%MINGW64%\bin\g++.exe" (
    echo MinGW not found at %MINGW64%
    echo Searching...
    for /d %%d in ("%LocalAppData%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*") do set "MINGW64=%%d\mingw64"
)
if not exist "%MINGW64%\bin\g++.exe" (
    echo Cannot find MinGW installation
    exit /b 1
)
set "PATH=%MINGW64%\bin;%PATH%"
cd /d "%~dp0"
if not exist build mkdir build
cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe"
cmake --build . --parallel
if %ERRORLEVEL% NEQ 0 (
    taskkill /f /im adocao.exe >nul 2>&1
    cmake --build . --parallel
)
echo.
echo Build done: %cd%\adocao.exe
