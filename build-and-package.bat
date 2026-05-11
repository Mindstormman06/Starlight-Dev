@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
cd /d "%ROOT%"

call :find_vs_tools
if errorlevel 1 goto :fail

if not defined VSCMD_ARG_TGT_ARCH (
    call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul
)

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] MSVC toolchain is not active. Open "x64 Native Tools Command Prompt for VS 2022" and rerun.
    goto :fail
)

echo [1/4] Configuring x64-release...
"%CMAKE_EXE%" --preset x64-release
if errorlevel 1 goto :fail

echo [2/4] Building...
"%CMAKE_EXE%" --build out/build/x64-release
if errorlevel 1 goto :fail

set "BUILD_DIR=%ROOT%out\build\x64-release"
if not exist "%BUILD_DIR%\src\Starlight.exe" (
    echo [ERROR] Build finished but "%BUILD_DIR%\src\Starlight.exe" was not found.
    goto :fail
)

pushd "%BUILD_DIR%"
if errorlevel 1 goto :fail

echo [3/4] Creating portable ZIP package...
"%CPACK_EXE%" -C Release -G ZIP
if errorlevel 1 (
    popd
    goto :fail
)

echo [4/4] Creating NSIS installer...
"%CPACK_EXE%" -C Release -G NSIS
if errorlevel 1 (
    echo [WARN] NSIS installer generation failed.
    echo [WARN] Install NSIS and re-run: winget install NSIS.NSIS
    echo [WARN] ZIP package was still generated in:
    echo        "%BUILD_DIR%"
    popd
    exit /b 1
)

popd
echo [DONE] Build + packages completed successfully.
echo        Output folder: "%BUILD_DIR%"
exit /b 0

:find_vs_tools
set "CMAKE_EXE="
set "CPACK_EXE="
set "VSROOT="

for %%E in (BuildTools Community Professional Enterprise) do (
    set "VSROOT_CANDIDATE=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\%%E"
    set "CMAKE_CANDIDATE=!VSROOT_CANDIDATE!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    set "CPACK_CANDIDATE=!VSROOT_CANDIDATE!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cpack.exe"
    if exist "!CMAKE_CANDIDATE!" if exist "!CPACK_CANDIDATE!" (
        set "VSROOT=!VSROOT_CANDIDATE!"
        set "CMAKE_EXE=!CMAKE_CANDIDATE!"
        set "CPACK_EXE=!CPACK_CANDIDATE!"
        goto :find_vs_tools_done
    )
)

:find_vs_tools_done
if not defined CMAKE_EXE (
    echo [ERROR] Could not find Visual Studio CMake tools.
    echo         Install Visual Studio 2022 Build Tools with C++ CMake tools.
    exit /b 1
)
exit /b 0

:fail
echo [FAILED] Build/package process failed.
exit /b 1
