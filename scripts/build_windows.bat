@echo off
REM SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
REM Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
REM
REM One-shot Windows build helper. Calls vcvars64.bat to set up MSVC env,
REM configures CMake with the windows-vcpkg preset, then builds Release.
REM
REM Requirements:
REM   - Visual Studio 2022 BuildTools or full VS install
REM   - vcpkg cloned and bootstrapped somewhere (default: C:\vcpkg)
REM   - CMake 3.26+ on PATH
REM
REM Usage:
REM   scripts\build_windows.bat
REM
REM Environment overrides:
REM   VCPKG_ROOT  vcpkg checkout (default C:\vcpkg)
REM   VS_VCVARS   path to vcvars64.bat (default standard VS 2022 BuildTools)

setlocal

if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:\vcpkg
if "%VS_VCVARS%"=="" set VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat

set REPO_ROOT=%~dp0..
pushd "%REPO_ROOT%"

if not exist "%VS_VCVARS%" (
    echo ERROR: vcvars64.bat not found at "%VS_VCVARS%"
    echo Set VS_VCVARS environment variable to point at your install.
    popd & exit /b 2
)
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo ERROR: vcpkg.exe not found at "%VCPKG_ROOT%"
    echo Set VCPKG_ROOT environment variable or clone vcpkg and bootstrap it.
    popd & exit /b 2
)

call "%VS_VCVARS%"
if errorlevel 1 ( popd & exit /b %errorlevel% )

cd /d "%REPO_ROOT%"
cmake --preset windows-vcpkg
if errorlevel 1 ( popd & exit /b %errorlevel% )

cmake --build build/windows-vcpkg --config Release -- /m
set BUILD_EXIT=%errorlevel%

popd
exit /b %BUILD_EXIT%
