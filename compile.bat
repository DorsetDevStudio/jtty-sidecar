@echo off
rem jtty-sidecar - build everything and leave the finished exe in dist\
rem
rem Needs the MSYS2 mingw64 toolchain (gfortran, gcc, FFTW). CMake and Ninja
rem are taken from MSYS2 when installed there, else from the PATH.
rem Run setup-toolchain.bat once if this script says a tool is missing.
setlocal
set MSYS=C:\msys64
if not exist "%MSYS%\mingw64\bin\gfortran.exe" goto :missing
if not exist "%MSYS%\mingw64\bin\gcc.exe" goto :missing
if not exist "%MSYS%\mingw64\lib\libfftw3f.a" goto :missing

set PATH=%MSYS%\mingw64\bin;%PATH%
cd /d "%~dp0"

where cmake >nul 2>nul || goto :nocmake
where ninja >nul 2>nul || goto :noninja

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=gcc -DCMAKE_Fortran_COMPILER=gfortran ^
  -DCMAKE_PREFIX_PATH=%MSYS%\mingw64
if errorlevel 1 exit /b 1
cmake --build build
if errorlevel 1 exit /b 1

if not exist dist mkdir dist
copy /Y build\jtty-sidecar.exe dist\ >nul
copy /Y build\jtty-shm-client.exe dist\ >nul
echo.
dist\jtty-sidecar.exe version
echo.
echo Built: %~dp0dist\jtty-sidecar.exe
exit /b 0

:missing
echo The MSYS2 mingw64 toolchain is not complete under %MSYS%.
echo Run setup-toolchain.bat first (installs gfortran, gcc and FFTW).
exit /b 1
:nocmake
echo cmake is not on the PATH. Install CMake (cmake.org) or run setup-toolchain.bat.
exit /b 1
:noninja
echo ninja is not on the PATH. Install Ninja (ninja-build.org, or "pip install ninja") or run setup-toolchain.bat.
exit /b 1
