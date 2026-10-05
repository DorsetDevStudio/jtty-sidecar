#!/usr/bin/env bash
# jtty-sidecar - self tests against the freshly built dist/ binaries (macOS, Linux).
# The same three groups as run-tests.bat:
#   1. in-memory loopbacks, clean and in noise
#   2. WAV round trip at 48 kHz
#   3. the shared-memory server driven by the reference client
set -u
cd "$(dirname "$0")"
D=${DIST:-dist}          # DIST=build-linux ./run-tests.sh tests another output folder
X=$D/jtty-sidecar
C=$D/jtty-shm-client
[ -x "$X" ] || { echo "run compile.sh first"; exit 1; }
FAILED=0
mkdir -p build

echo "--- loopback, clean (and timed: a decoder start of many seconds is a broken FFTW, see compile.sh)"
start=$SECONDS
$X loopback "CQ G5STU CQ" || FAILED=1
took=$((SECONDS - start))
echo "first decode took ${took} s"
if [ $took -gt 5 ]; then echo "FAIL: the first decode took ${took} s (expected under 1 s)"; FAILED=1; fi
echo "--- loopback, two frames"
$X loopback "WB9XYZ 599 123" || FAILED=1
echo "--- loopback, -12 dB, RTTY Roundup profile"
$X loopback "TU NOW JA6DEF 599 102" --snr -12 --profile rtty-roundup || FAILED=1
echo "--- loopback, free text, -8 dB"
$X loopback "HELLO FROM THE SHACK 73" --snr -8 || FAILED=1

echo "--- wav round trip at 48 kHz"
$X encode "G5STU 599 001" --out build/roundtrip.wav --rate 48000 --profile rtty-roundup || FAILED=1
$X decode build/roundtrip.wav || FAILED=1

echo "--- shared memory server + reference client"
name="jtty-selftest-$$"
$X serve "$name" &
server=$!
$C "$name" "CQ TEST G5STU IO91" || FAILED=1
# The client tells the server to quit when it finishes; a client that failed early never did.
sleep 1
kill $server 2>/dev/null || true
wait $server 2>/dev/null || true

echo
if [ $FAILED -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $FAILED
