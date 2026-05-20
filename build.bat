@echo off
setlocal
set "PORTABLE=0"
set "HIGHFPS=0"
:parse
if "%~1"=="" goto :done
if /I "%~1"=="portable" set "PORTABLE=1"
if /I "%~1"=="highfps" set "HIGHFPS=1"
shift
goto :parse
:done

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

set "OPTS=-DCMAKE_BUILD_TYPE=Release"
if "%PORTABLE%"=="1" set "OPTS=%OPTS% -DADOCAO_PORTABLE=ON"
if "%HIGHFPS%"=="1"  set "OPTS=%OPTS% -DADOCAO_HIGH_FPS=ON"

echo === Building ADOCAO (portable=%PORTABLE% highfps=%HIGHFPS%) ===
cmake .. -G "MinGW Makefiles" -DCMAKE_C_COMPILER="%MINGW64%\bin\gcc.exe" -DCMAKE_CXX_COMPILER="%MINGW64%\bin\g++.exe" %OPTS%

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
