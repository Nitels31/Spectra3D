@echo off
setlocal
rem Thin wrapper around CMake - see CMakeLists.txt for the real build definition.
if not exist build mkdir build

rem The MSYS2 UCRT64 toolchain, when it is installed, is the one this project is
rem developed against - and it is not necessarily the g++ that happens to be first
rem in PATH. A machine with two MinGW installs builds with the wrong one without
rem a word, and the binary then fails to start next to the other one's runtime:
rem the two builds disagree about which thread-local variables exist, and the
rem loader reports a missing entry point instead of a wrong toolchain.
rem
rem MSYS2 lets its root move, so MSYS2_ROOT is honoured and C:\msys64 is the
rem fallback. A machine without MSYS2 keeps whatever PATH already had.
set "SPECTRA_MSYS2=%MSYS2_ROOT%"
if not defined SPECTRA_MSYS2 set "SPECTRA_MSYS2=C:\msys64"
set "SPECTRA_UCRT=%SPECTRA_MSYS2%\ucrt64\bin"
if exist "%SPECTRA_UCRT%\g++.exe" (
    rem In front, so cmake, ninja and g++ all come from the same toolchain.
    set "PATH=%SPECTRA_UCRT%;%PATH%"
)

rem %~dp0 ends with a backslash, and "\"C:\"..." would be read by the Windows
rem argument parser as an escaped quote, gluing every flag onto one argument.
rem The trailing dot cancels the backslash: "C:\__Spectra3D\."
cmake -S "%~dp0." -B "%~dp0build" -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build" --config Release %*
if errorlevel 1 exit /b 1
echo.
echo Build OK: build\spectra3d.exe
