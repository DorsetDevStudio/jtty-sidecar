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

Only Windows x64 is built at the moment. Nothing in the code is Windows
specific apart from `src/shm_win.c`; other platforms can follow once the
upstream mode is released.

## Downloads

Every push is built by GitHub Actions on Windows x64 with the same MSYS2 toolchain
as compile.bat, and the self tests must pass. A tag `vX.Y.Z` publishes a release:

* `jtty-sidecar-win64.zip` - always the newest release, at
  `https://github.com/DorsetDevStudio/jtty-sidecar/releases/latest/download/jtty-sidecar-win64.zip`
* `jtty-sidecar-X.Y.Z-win64.zip` - that version, kept
* `SHA256SUMS.txt` - checksums of both

The zip holds `jtty-sidecar.exe`, `jtty-shm-client.exe`, README.md, LICENSE and
UPSTREAM.md. Nothing to install; no DLLs needed.

## Building

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

The server creates one Windows file mapping named `<name>` holding a single
`JttyShm` structure, and three auto-reset events `<name>.rx`, `<name>.res`
and `<name>.tx`. A client opens them, checks `magic`, `version` and
`struct_size`, and then:

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
