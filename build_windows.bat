@echo off
setlocal
rem Thin wrapper around CMake - see CMakeLists.txt for the real build definition.
if not exist build mkdir build
rem %~dp0 ends with a backslash, and "\"C:\"..." would be read by the Windows
rem argument parser as an escaped quote, gluing every flag onto one argument.
rem The trailing dot cancels the backslash: "C:\__Spectra3D\."
cmake -S "%~dp0." -B "%~dp0build" -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build" --config Release %*
if errorlevel 1 exit /b 1
echo.
echo Build OK: build\spectra3d.exe