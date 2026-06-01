@echo off
REM Cadenza build helper.
REM Usage:  build.bat [cmake-build-target ...]
REM        (with no args, builds the default target)
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
    echo Failed to initialise MSVC environment.
    exit /b 1
)

if "%~1"=="" (
    cmake --build build-msvc
) else (
    cmake --build build-msvc --target %*
)
endlocal
