@echo off
rem jtty-sidecar - self tests against the freshly built dist\ binaries.
rem   1. in-memory loopbacks, clean and in noise
rem   2. WAV round trip at 48 kHz
rem   3. the shared-memory server driven by the reference client
setlocal
cd /d "%~dp0"
set X=dist\jtty-sidecar.exe
set C=dist\jtty-shm-client.exe
if not exist %X% echo run compile.bat first & exit /b 1
set FAILED=0

echo --- loopback, clean
%X% loopback "CQ G5STU CQ" || set FAILED=1
echo --- loopback, two frames
%X% loopback "WB9XYZ 599 123" || set FAILED=1
echo --- loopback, -12 dB, RTTY Roundup profile
%X% loopback "TU NOW JA6DEF 599 102" --snr -12 --profile rtty-roundup || set FAILED=1
echo --- loopback, free text, -8 dB
%X% loopback "HELLO FROM THE SHACK 73" --snr -8 || set FAILED=1

echo --- wav round trip at 48 kHz
%X% encode "G5STU 599 001" --out build\roundtrip.wav --rate 48000 --profile rtty-roundup || set FAILED=1
%X% decode build\roundtrip.wav || set FAILED=1

echo --- shared memory server + reference client
start "" /b %X% serve jtty-selftest
%C% jtty-selftest "CQ TEST G5STU IO91" || set FAILED=1

echo.
if %FAILED%==0 (echo ALL TESTS PASSED) else (echo SOME TESTS FAILED)
exit /b %FAILED%
