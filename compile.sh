#!/usr/bin/env bash
# jtty-sidecar - build on macOS or Linux and leave the finished program in dist/
#
#   ./compile.sh                 native build for this machine
#   ./compile.sh --universal     macOS only: build arm64 and x86_64 and join them with lipo
#                                (needs a gfortran for BOTH architectures: Homebrew gcc under
#                                /opt/homebrew for arm64 and under /usr/local for x86_64)
#
# Needs: gfortran, gcc/clang, FFTW with its static library, CMake 3.16+ and Ninja.
#   macOS   brew install gcc fftw cmake ninja
#   Debian  sudo apt install gfortran build-essential libfftw3-dev cmake ninja-build
#   Fedora  sudo dnf install gcc-gfortran fftw-static cmake ninja-build
set -euo pipefail
cd "$(dirname "$0")"

os=$(uname -s)
arch=$(uname -m)

# macOS: the oldest release the program must run on, for BOTH architectures. Station Master Pro
# itself targets macOS 12, so the engine does too. (Must be in the environment before CMake first
# configures, which is why it is here and not in CMakeLists.txt.)
if [ "$os" = Darwin ]; then
    export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-12.0}
fi

# A full Homebrew prefix for one architecture: the gfortran in it and the FFTW beside it.
brew_prefix_for() {
    case "$1" in
        arm64)  echo /opt/homebrew ;;
        x86_64) echo /usr/local ;;
    esac
}

# Homebrew's gcc formula: "gfortran" is its newest Fortran, and the matching C compiler is
# "gcc-<major>" (plain "gcc" on a Mac is Apple's clang). Sets fc and cc.
brew_compilers() {
    fc="$1/bin/gfortran"
    [ -x "$fc" ] || return 1
    cc="$1/bin/gcc-$("$fc" -dumpversion | cut -d. -f1)"
    [ -x "$cc" ] || return 1
}

# Linux on ARM: FFTW is built from source, because the distributions' packages make the decoder's
# first FFT plans take about half a minute. Measured 2026-10-05 on a Raspberry Pi 5, same engine,
# first decode: Debian bookworm's libfftw3f 33.5 s, Ubuntu 22.04's 33.5 s, FFTW 3.3.10 built here
# 0.47 s. (The upstream decoder plans with FFTW_MEASURE; x86 and macOS packages are unaffected.)
# Prints the install prefix; everything else goes to build-fftw/*.log.
FFTW_VERSION=3.3.10
FFTW_SHA256=56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467
fftw_from_source() {
    local top=$PWD/build-fftw prefix=$PWD/build-fftw/prefix
    if [ -f "$prefix/lib/libfftw3f.a" ]; then echo "$prefix"; return; fi
    mkdir -p "$top"
    local tarball=$top/fftw-$FFTW_VERSION.tar.gz
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "https://www.fftw.org/fftw-$FFTW_VERSION.tar.gz" >&2
    echo "$FFTW_SHA256  $tarball" | sha256sum -c --quiet - >&2 || { echo "FFTW download checksum mismatch" >&2; exit 1; }
    rm -rf "$top/src" && mkdir -p "$top/src" && tar xzf "$tarball" -C "$top/src" --strip-components=1
    echo "building FFTW $FFTW_VERSION from source (logs in $top)" >&2
    (cd "$top/src" && ./configure --prefix="$prefix" --enable-float --enable-static --disable-shared \
        --enable-neon > "$top/configure.log" 2>&1 && make -j"$(nproc)" > "$top/make.log" 2>&1 \
        && make install > "$top/install.log" 2>&1) || { echo "FFTW build failed, see $top" >&2; exit 1; }
    echo "$prefix"
}

configure_and_build() {   # <build dir> <extra cmake args...>
    local dir=$1; shift
    cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
    cmake --build "$dir"
}

mkdir -p dist

if [ "$os" = Darwin ] && [ "${1:-}" = "--universal" ]; then
    for a in arm64 x86_64; do
        prefix=$(brew_prefix_for $a)
        if [ "$a" = "$arch" ]; then runner=(); else runner=(arch -"$a"); fi
        brew_compilers "$prefix" || { echo "no gfortran under $prefix for $a (brew install gcc fftw in that prefix)"; exit 1; }
        ${runner[@]+"${runner[@]}"} cmake -S . -B "build-$a" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_OSX_ARCHITECTURES=$a \
            -DCMAKE_Fortran_COMPILER="$fc" -DCMAKE_C_COMPILER="$cc" -DCMAKE_PREFIX_PATH="$prefix"
        ${runner[@]+"${runner[@]}"} cmake --build "build-$a"
    done
    lipo -create build-arm64/jtty-sidecar build-x86_64/jtty-sidecar -output dist/jtty-sidecar
    lipo -create build-arm64/jtty-shm-client build-x86_64/jtty-shm-client -output dist/jtty-shm-client
    codesign --force --sign - dist/jtty-sidecar dist/jtty-shm-client    # ad hoc: required to run on Apple Silicon
    lipo -info dist/jtty-sidecar
else
    args=()
    if [ "$os" = Darwin ]; then
        prefix=$(brew_prefix_for "$arch")
        brew_compilers "$prefix" || { echo "no gfortran under $prefix (brew install gcc fftw)"; exit 1; }
        args=(-DCMAKE_Fortran_COMPILER="$fc" -DCMAKE_C_COMPILER="$cc" -DCMAKE_PREFIX_PATH="$prefix")
    elif [ "$arch" = aarch64 ] && [ -z "${JTTY_SYSTEM_FFTW:-}" ]; then
        fftw=$(fftw_from_source)
        args=(-DFFTW3F_LIB="$fftw/lib/libfftw3f.a" -DFFTW3_INCLUDE_DIR="$fftw/include")
    fi
    configure_and_build build ${args[@]+"${args[@]}"}
    cp build/jtty-sidecar build/jtty-shm-client dist/
    if [ "$os" = Darwin ]; then codesign --force --sign - dist/jtty-sidecar dist/jtty-shm-client; fi
fi

echo
dist/jtty-sidecar version
echo
echo "Built: $(pwd)/dist/jtty-sidecar"
