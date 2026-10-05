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
    fi
    configure_and_build build ${args[@]+"${args[@]}"}
    cp build/jtty-sidecar build/jtty-shm-client dist/
    if [ "$os" = Darwin ]; then codesign --force --sign - dist/jtty-sidecar dist/jtty-shm-client; fi
fi

echo
dist/jtty-sidecar version
echo
echo "Built: $(pwd)/dist/jtty-sidecar"
