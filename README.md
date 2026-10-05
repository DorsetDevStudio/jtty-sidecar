# jtty-sidecar

A JTTY modem that runs as its own process. JTTY is the keyboard and contest
digital mode introduced in WSJT-X 3.2 by Joe Taylor K1JT and the WSJT
Development Group. This program wraps their unmodified encoder and decoder in
a small command-line front end and a shared-memory server, so any logging or
rig-control application can transmit and receive JTTY without linking the
mode's code into itself.

It is free software under the GNU General Public License v3 (see LICENSE),
the same licence as the upstream code it contains.

## What it does

    jtty-sidecar decode <file.wav> [--f0 Hz] [--ftol Hz] [--nfa Hz] [--nfb Hz] [--json]
    jtty-sidecar encode "<text>" --out <file.wav> [--rate 12000|48000] [--f0 Hz]
                 [--profile unknown|field-day|rtty-roundup] [--chained]
    jtty-sidecar loopback "<text>" [--snr dB] [--profile ...]
    jtty-sidecar serve <shm-name> [--parent <pid>] [--verbose]
    jtty-sidecar version

* `decode` streams a 12 kHz (or 48 kHz) PCM16 WAV file through the live
  decoder in 100 ms blocks, exactly as a real-time client would, and prints
  every message update: time, audio frequency, message id, an `*` when the
  end-of-message frame arrived, and the text.
* `encode` renders a message to a WAV file, showing the canonical text and
  the number of 1.888 s frames it took.
* `loopback` encodes and decodes in memory, optionally through additive
  Gaussian noise at a given SNR, as a quick self-test.
* `serve` is the real-time interface. It creates a named shared-memory
  segment and three named events, decodes receive audio the client streams
  into the segment, publishes results into it, and renders transmit audio on
  request. See **Shared-memory protocol** below and `src/protocol.h`.

Built for:

* **Windows x64** - also runs on Windows on ARM under its x64 emulation
* **macOS** - one universal binary, Apple Silicon and Intel, macOS 12 or newer
* **Linux x86_64 and aarch64** (64-bit Raspberry Pi OS and other arm64
  boards) - glibc 2.35 or newer: Ubuntu 22.04, Debian 12 bookworm and later

Everything is the same code apart from the shared-memory layer:
`src/shm_win.c` on Windows, `src/shm_posix.c` on macOS and Linux, behind
`src/shm.h`. The segment's bytes are identical on every platform.

## Downloads

Every push is built by GitHub Actions on every platform, and the self tests
must pass on each (the macOS slices are tested on their own architecture, then
joined). A tag `vX.Y.Z` publishes a release. Each platform has a bare executable
plus its SHA-256 (bare hex, for a program that fetches just the engine) and a
zip with the reference client, README.md, LICENSE and UPSTREAM.md:

| Platform | Executable | Zip |
|---|---|---|
| Windows x64 | `jtty-sidecar.exe` (Authenticode-signed, uploaded by deploy.bat) | `jtty-sidecar-win64.zip` |
| macOS universal | `jtty-sidecar-macos-universal` (ad hoc signed) | `jtty-sidecar-macos.zip` |
| Linux x86_64 | `jtty-sidecar-linux-x86_64` | `jtty-sidecar-linux-x86_64.zip` |
| Linux aarch64 | `jtty-sidecar-linux-aarch64` | `jtty-sidecar-linux-aarch64.zip` |

The newest release is always at
`https://github.com/DorsetDevStudio/jtty-sidecar/releases/latest/download/<file>`,
the SHA-256 at `<file>.sha256`, and `SHA256SUMS.txt` lists every file. Nothing to
install. The Windows program needs no DLLs; the macOS and Linux programs need
only the system C library (the Fortran runtime and FFTW are linked in). On
macOS and Linux a downloaded program must be made executable (`chmod +x`).

## Releasing

    deploy.bat            next patch version (0.1.0 -> 0.1.1)
    deploy.bat minor      next minor version
    deploy.bat major      next major version
    deploy.bat 1.4.2      exactly that version

deploy.bat bumps the version in CMakeLists.txt, builds, runs the self tests,
signs the executable (signtool, publisher certificate on a hardware token),
commits everything as "Release vX.Y.Z", tags, pushes main and the tag, and
creates the GitHub Release with the signed exe and its SHA-256 (GitHub CLI,
`gh auth login` once). GitHub Actions then builds the tag and adds the zip
(see Downloads). A build, test or signing failure stops it before anything is
committed.

## Building

### macOS and Linux

    # macOS:  brew install gcc fftw cmake ninja
    # Debian: sudo apt install gfortran build-essential libfftw3-dev cmake ninja-build
    ./compile.sh                 builds dist/jtty-sidecar and dist/jtty-shm-client for this machine
    ./compile.sh --universal     macOS: both architectures joined with lipo (needs Homebrew gcc
                                 and fftw under /opt/homebrew AND /usr/local)
    ./run-tests.sh               the same self tests as run-tests.bat

gfortran builds one architecture at a time, which is why the universal binary
is two builds joined afterwards. The macOS deployment target is 12.0 for both.

On Linux ARM (Raspberry Pi and other aarch64 boards) compile.sh downloads FFTW
3.3.10, checks its SHA-256 and builds it into `build-fftw/`, instead of using
the distribution's package: with Debian's or Ubuntu's ARM FFTW the decoder's
first FFT plans take about 33 seconds on every start (measured on a Pi 5;
0.5 s with FFTW built here). `JTTY_SYSTEM_FFTW=1 ./compile.sh` uses the
package anyway. run-tests.sh fails if the first decode takes over 5 seconds.

### Windows

Prerequisites: [MSYS2](https://www.msys2.org) at `C:\msys64`, plus CMake and
Ninja on the PATH (or installed into MSYS2).

    setup-toolchain.bat      once: installs gfortran, gcc, FFTW, CMake and Ninja into MSYS2
    compile.bat              builds and leaves dist\jtty-sidecar.exe and dist\jtty-shm-client.exe
    run-tests.bat            loopbacks, a WAV round trip, and a live server driven by the client

The executables are statically linked. They need no DLLs.

If compile.bat reports that gcc "is not able to compile a simple test
program", some other installer has probably left an old `libwinpthread-1.dll`
in `C:\Windows\System32`, which shadows the MSYS2 one. setup-toolchain.bat
detects this and places the correct DLL beside the compiler back ends.

## Shared-memory protocol

The contract is `src/protocol.h`; this is the summary.

The server creates one shared-memory segment named `<name>` holding a single
`JttyShm` structure, and three wake-up objects `<name>.rx`, `<name>.res` and
`<name>.tx`. On Windows those are a file mapping and three auto-reset events.
On macOS and Linux they are a POSIX shared-memory object `/<name>` and three
named semaphores `/<name>.rx` etc., each kept at a count of at most one so it
behaves like an auto-reset event; `<name>` is at most 25 characters there
(macOS's limit), and macOS has no `sem_timedwait`, so a timed wait polls. The
reference client `tests/shm_client.c` shows both. A client opens them, checks
`magic`, `version` and `struct_size`, and then:

* **Receive.** Writes 12 kHz mono int16 audio into the ring
  `rx_pcm[rx_written % JTTY_RX_RING_SAMPLES]`, advances `rx_written` after
  the samples are in place, and sets `<name>.rx`. The server decodes every
  block as it arrives. Each new or extended message is appended to
  `results[results_written % JTTY_RESULT_RING]`, `results_written` is
  incremented and `<name>.res` set. A result carries the upstream message id
  (stable while a message grows), the absolute start position in the client's
  own sample count, the audio frequency, the text so far and an end-of-message
  flag.
* **Transmit.** Fills `tx_request` (text, exchange profile, sample rate 12000
  or 48000, lowest tone frequency, chained flag) and increments
  `tx_request.seq`. The server renders the audio into `tx_pcm`, fills
  `tx_response` (status, sample and frame counts, canonical text), sets
  `tx_response.seq_done` to the request's seq and signals `<name>.tx`.
* **Control.** `command` takes `JTTY_CMD_QUIT` or `JTTY_CMD_RESET`;
  `nfa`, `nfb`, `f0` and `ftol` steer the decoder's search and may be changed
  at any time; `heartbeat` ticks every server loop; `server_state` reports
  starting, running or quitting. With `--parent <pid>` the server exits by
  itself when that process ends.

Message text, timing and ids are exactly what the upstream decoder reports;
the server adds nothing and interprets nothing.

## Upstream code and how to update it

`upstream/wsjtx/` holds the JTTY sources (`lib/jtty/`) and the handful of
shared WSJT-X routines they call, copied byte for byte from the WSJT-X source
tree apart from line-ending normalisation. `upstream/UPSTREAM.md` records the
version and a hash per file. Nothing in that folder is ever edited; all glue
lives in `src/`.

To take a new upstream release:

    python tools/vendor_upstream.py <path-to-unpacked-wsjtx-source> "<version label>"
    compile.bat
    dist\jtty-sidecar.exe loopback "CQ K1ABC CQ"

If the upstream entry points change (the five routines in `src/jtty_api.h`),
`src/decoder.c` and `src/encoder.c` are the only places to touch.

The WS - Digital Mode Suite by Uwe Risse DG2YCB ships the same `lib/jtty`
folder as WSJT-X and can be vendored from in the same way.

## Status

JTTY itself is pre-release. Both WSJT-X 3.2.0-rc1 and WS 3.2.1 describe it as
still in development and say the protocol may change. This program tracks
whatever the vendored drop implements and will be re-vendored when the mode
is finalised.
