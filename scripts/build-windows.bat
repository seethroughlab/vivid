@echo off
REM Build script for Windows
REM Builds the Vivid runtime and VS Code extension

setlocal enabledelayedexpansion

echo === Building Vivid for Windows ===

set SCRIPT_DIR=%~dp0
set PROJECT_ROOT=%SCRIPT_DIR%..

echo Project root: %PROJECT_ROOT%

REM Check prerequisites
echo.
echo Checking prerequisites...

where cmake >nul 2>&1 || (
    echo Error: cmake is required but not installed.
    echo Install from https://cmake.org/download/
    exit /b 1
)

where cl >nul 2>&1 || (
    echo Error: MSVC compiler not found.
    echo Run this script from a Visual Studio Developer Command Prompt
    echo or install Visual Studio with C++ workload.
    exit /b 1
)

where node >nul 2>&1 || (
    echo Error: node is required but not installed.
    echo Install from https://nodejs.org
    exit /b 1
)

where npm >nul 2>&1 || (
    echo Error: npm is required but not installed.
    echo Install from https://nodejs.org
    exit /b 1
)

echo All prerequisites found.

REM Build runtime
echo.
echo === Building Runtime ===
cd /d "%PROJECT_ROOT%"

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=Release
echo Build type: %BUILD_TYPE%

cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
cmake --build build --config %BUILD_TYPE% -j

echo.
echo Runtime built: %PROJECT_ROOT%\build\bin\%BUILD_TYPE%\vivid.exe

REM Build VS Code extension
echo.
echo === Building VS Code Extension ===
cd /d "%PROJECT_ROOT%\extension"

call npm install
call npm run compile

REM Build native module
echo.
echo === Building Native Module ===
cd /d "%PROJECT_ROOT%\extension\native"

call npm install
call npm run build

echo.
echo === Build Complete ===
echo.
echo Runtime: %PROJECT_ROOT%\build\bin\%BUILD_TYPE%\vivid.exe
echo Extension: %PROJECT_ROOT%\extension (run 'npm run package' to create .vsix)
echo.
echo To test:
echo   1. Open extension folder in VS Code and press F5
echo   2. In the Extension Development Host, open an example project
echo   3. Run: %PROJECT_ROOT%\build\bin\%BUILD_TYPE%\vivid.exe examples\hello

endlocal
