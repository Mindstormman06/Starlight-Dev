@echo off
REM Shared setup for Windows builds: MSVC toolset, VS-bundled CMake/CPack, Ninja on PATH.
REM Call from the repo root:  call "%~dp0scripts\vswin-init.bat"
REM On success sets: VSROOT, CMAKE_EXE, CPACK_EXE (CPack may be missing on minimal installs).

set "VSROOT="
set "CMAKE_EXE="
set "CPACK_EXE="

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do (
        set "VSROOT=%%I"
        goto :vs_found
    )
)

for %%E in (BuildTools Community Professional Enterprise Preview) do (
    if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E\Common7\Tools\VsDevCmd.bat" (
        set "VSROOT=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E"
        goto :vs_found
    )
)

echo [ERROR] Visual Studio 2022 with MSVC ^(v143^) was not found.
echo         Install "Desktop development with C++" or "Build Tools for Visual Studio 2022"
echo         with the MSVC x64/x86 tools and a Windows 10/11 SDK.
exit /b 1

:vs_found
set "CMAKE_EXE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "CPACK_EXE=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe"
set "VS_NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "%CMAKE_EXE%" (
    echo [ERROR] VS-bundled CMake was not found: "%CMAKE_EXE%"
    echo         In Visual Studio Installer, add the "C++ CMake tools for Windows" component.
    exit /b 1
)

call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 exit /b 1

if exist "%VS_NINJA%" set "PATH=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] cl.exe is not on PATH after VsDevCmd. MSVC install may be incomplete.
    exit /b 1
)

exit /b 0
