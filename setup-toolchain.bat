@echo off
rem jtty-sidecar - one-off toolchain install.
rem
rem Installs the mingw64 packages the build needs into an existing MSYS2 at
rem C:\msys64 (https://www.msys2.org). The compiler itself is the one thing
rem not kept inside this folder: a Fortran toolchain is about a gigabyte, and
rem MSYS2 keeps it patched. Everything else (upstream code, FFTW link) is
rem resolved from here by compile.bat.
setlocal
set MSYS=C:\msys64
if not exist "%MSYS%\usr\bin\pacman.exe" (
  echo MSYS2 not found at %MSYS%. Install it from https://www.msys2.org and re-run.
  exit /b 1
)
"%MSYS%\usr\bin\pacman.exe" -S --needed --noconfirm ^
  mingw-w64-x86_64-gcc mingw-w64-x86_64-gcc-fortran mingw-w64-x86_64-fftw ^
  mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
if errorlevel 1 exit /b 1

rem Some installers drop an old MinGW libwinpthread-1.dll into
rem C:\Windows\System32. Windows searches System32 before the PATH, so that
rem stale copy shadows the MSYS2 one and the gcc back ends (cc1, f951) die with
rem "entry point not found". The application directory wins the search, so put
rem the right DLL beside the back ends. Harmless when the problem is absent.
if exist "%SystemRoot%\System32\libwinpthread-1.dll" (
  for /d %%D in ("%MSYS%\mingw64\lib\gcc\x86_64-w64-mingw32\*") do (
    copy /Y "%MSYS%\mingw64\bin\libwinpthread-1.dll" "%%D\" >nul
    echo Shadowed pthread DLL found in System32; placed the MSYS2 copy in %%D
  )
)
echo Toolchain ready. Now run compile.bat
exit /b 0
