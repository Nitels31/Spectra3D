@echo off
setlocal
rem Thin wrapper around CMake - see CMakeLists.txt for the real build definition.
if not exist build mkdir build
cmake -S "%~dp0" -B "%~dp0build" -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b 1
cmake --build "%~dp0build" --config Release %*
if errorlevel 1 exit /b 1
echo.
echo Build OK: build\spectra3d.exe