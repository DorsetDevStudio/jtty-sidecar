cmake_minimum_required (VERSION 3.20)
project (jtty_sidecar VERSION 0.1.0 LANGUAGES C Fortran)

# The upstream drop this build wraps. Keep in step with upstream/UPSTREAM.md.
set (JTTY_UPSTREAM_LABEL "WSJT-X 3.2.0-rc1 lib/jtty (= WS 3.2.1)" CACHE STRING "Upstream version label baked into --version")

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release)
endif ()

set (CMAKE_Fortran_MODULE_DIRECTORY ${CMAKE_BINARY_DIR}/mod)

# FFTW single precision, static. The MSYS2 package ships libfftw3f.a next to
# the import library; name the archive explicitly so the exe has no DLL needs.
find_library (FFTW3F_LIB NAMES libfftw3f.a fftw3f REQUIRED)
find_path (FFTW3_INCLUDE_DIR fftw3.f03 REQUIRED)

set (UP ${CMAKE_SOURCE_DIR}/upstream/wsjtx)

# ---------------------------------------------------------------------------
# libjtty: the unmodified upstream mode code plus the shared WSJT-X routines
# it calls. The four standalone programs in lib/jtty (jtty, rjtty, sjtty,
# sjtty_qrm), the PortAudio glue and the jtty_codewords.f90 data fragment
# (an unused include-style table) are not part of the library.
# ---------------------------------------------------------------------------
set (JTTY_FSRCS
  ${UP}/lib/fftw3mod.f90
  ${UP}/lib/four2a.f90
  ${UP}/lib/db.f90
  ${UP}/lib/77bit/packjt77_grammar.f90
  ${UP}/lib/77bit/packjt77_schema.f90
  ${UP}/lib/77bit/packjt77.f90
  ${UP}/lib/chkcall.f90
  ${UP}/lib/fmtmsg.f90
  ${UP}/lib/twkfreq.f90
  ${UP}/lib/smo121.f90
  ${UP}/lib/wavhdr.f90
  ${UP}/lib/gfsk_pulse.f90
  ${UP}/lib/pctile.f90
  ${UP}/lib/shell.f90
  ${UP}/lib/jtty/jttycom.f90
  ${UP}/lib/jtty/tbcc.f90
  ${UP}/lib/jtty/jtty_tbcc_code_profile.f90
  ${UP}/lib/jtty/jtty_fec_mod.f90
  ${UP}/lib/jtty/jtty_tbcc_decoder.f90
  ${UP}/lib/jtty/jtty_tbcc_list_decoder.f90
  ${UP}/lib/jtty/jtty_payload_correlators.f90
  ${UP}/lib/jtty/jtty_source_codec.f90
  ${UP}/lib/jtty/jtty_mod.f90
  ${UP}/lib/jtty/jtty_block_pow.f90
  ${UP}/lib/jtty/jtty_peakup.f90
  ${UP}/lib/jtty/jtty_spec.f90
  ${UP}/lib/jtty/ana64a.f90
  ${UP}/lib/jtty/gen_syncwave.f90
  ${UP}/lib/jtty/gen_jttywave.f90
  ${UP}/lib/jtty/genjtty.f90
  ${UP}/lib/jtty/subtract_jtty.f90
  ${UP}/lib/jtty/jtty_decode.f90
  ${UP}/lib/jtty/jtty_mdecode.f90
  ${UP}/lib/jtty/rjtty_sub.f90
)

add_library (jtty STATIC ${JTTY_FSRCS})
target_include_directories (jtty PRIVATE ${FFTW3_INCLUDE_DIR})
target_compile_options (jtty PRIVATE
  -O2 -fno-range-check -ffree-line-length-none -ffixed-line-length-none
  -Wno-tabs -Wno-conversion -Wno-unused-dummy-argument)
target_link_libraries (jtty PUBLIC ${FFTW3F_LIB})

# ---------------------------------------------------------------------------
# jtty-sidecar: the C front end (CLI tools + shared-memory server).
# ---------------------------------------------------------------------------
add_executable (jtty-sidecar
  src/main.c
  src/wav.c
  src/decoder.c
  src/encoder.c
  src/shm_win.c
  src/server.c
)
target_compile_definitions (jtty-sidecar PRIVATE
  JTTY_SIDECAR_VERSION="${PROJECT_VERSION}"
  JTTY_UPSTREAM_LABEL="${JTTY_UPSTREAM_LABEL}")
target_compile_options (jtty-sidecar PRIVATE -O2 -Wall -Wextra)
target_link_libraries (jtty-sidecar PRIVATE jtty)
# Link with the Fortran driver so libgfortran/libquadmath come in, and fully
# static so dist/ holds one self-contained exe.
set_target_properties (jtty-sidecar PROPERTIES LINKER_LANGUAGE Fortran)
target_link_options (jtty-sidecar PRIVATE -static -static-libgcc -static-libgfortran)

# ---------------------------------------------------------------------------
# jtty-shm-client: reference client for the shared-memory protocol, used by
# run-tests.bat to drive a live server end to end. Plain C, no Fortran.
# ---------------------------------------------------------------------------
add_executable (jtty-shm-client tests/shm_client.c)
target_compile_options (jtty-shm-client PRIVATE -O2 -Wall -Wextra)
target_link_options (jtty-shm-client PRIVATE -static)
