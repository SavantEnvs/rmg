#!/usr/bin/env bash
#
# mayhem/build.sh — builds the RMG-Core fuzz harnesses (sanitized + DWARF 3) and the functional self-test
# (normal flags). RMG-Core and the bundled LZMA SDK are compiled straight from their sources: the GUI,
# plugins and mupen64plus-core are not needed (RMG-Core reaches the emulator core through dlopen only).
# Runs in /mayhem as `mayhem`; offline (apt packages libminizip-dev/zlib1g-dev come from the image).
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
CORE="$SRC/Source/RMG-Core"
LZMA="$SRC/Source/3rdParty/lzma"
OUT="$SRC/mayhem/build"
HARNESSES=(fuzz_zip fuzz_7z fuzz_cheat fuzz_romcache)

LZMA_SOURCES=(Bcj2.c Bra.c Bra86.c BraIA64.c CpuArch.c Delta.c Lzma2Dec.c LzmaDec.c Ppmd7.c Ppmd7Dec.c
              7zCrc.c 7zCrcOpt.c Sha256.c Sha256Opt.c 7zAlloc.c 7zArcIn.c 7zBuf.c 7zBuf2.c 7zDec.c 7zFile.c 7zStream.c)
LZMA_DEFS="-DZ7_PPMD_SUPPORT -DNDEBUG -D_REENTRANT -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE"
CORE_SOURCES=("$CORE"/*.cpp "$CORE"/m64p/*.cpp)

rm -rf "$OUT"
mkdir -p "$OUT/gen" "$OUT/obj-fuzz" "$OUT/obj-plain"

# RMG-Core's generated Config.hpp (distro-style, non-portable install).
cat > "$OUT/gen/Config.hpp" <<'CFG'
#ifndef CORE_CONFIG_HPP
#define CORE_CONFIG_HPP
#define CORE_VERSION "mayhem"
#define CORE_INSTALL_LIBDIR  "/usr/lib"
#define CORE_INSTALL_DATADIR "/usr/share"
#endif // CORE_CONFIG_HPP
CFG

CORE_INC=(-I"$OUT/gen" -I"$CORE" -I"$SRC/Source")
CORE_DEFS=(-DNETPLAY)

# compile_all <objdir> <c flags> -- <c++ flags>: compile every LZMA + RMG-Core source in parallel.
compile_all() {
  local objdir="$1" cflags="$2" cxxflags="$3"
  {
    for f in "${LZMA_SOURCES[@]}"; do
      printf '%s\0' "$CC" $cflags $LZMA_RELAX $LZMA_DEFS -I"$LZMA" -c "$LZMA/$f" -o "$objdir/lzma_${f%.c}.o"
    done
    for f in "${CORE_SOURCES[@]}"; do
      o="$objdir/core_$(basename "$(dirname "$f")")_$(basename "${f%.cpp}").o"
      printf '%s\0' "$CXX" -std=c++20 $cxxflags "${CORE_DEFS[@]}" "${CORE_INC[@]}" -c "$f" -o "$o"
    done
  } | tr '\0' ' ' | sed 's/ \(clang\)/\n\1/g' | xargs -P "$MAYHEM_JOBS" -I{} bash -c '{}'
}

# 1) Sanitized + coverage-instrumented project objects (libFuzzer sees coverage inside RMG-Core).
# Relaxed UBSan checks (everything else stays halting):
#  - alignment, vendored LZMA SDK only: 7zArcIn.c does unaligned UInt64 loads (benign on x86) that would
#    abort the 7z target on its own seed.
#  - bool,enum, RMG-Core + harnesses: the ROM cache reader fread()s raw file bytes straight into bool/enum
#    members; the invalid-value load is benign and would otherwise mask the real memory bugs behind it.
LZMA_RELAX="-fno-sanitize=alignment"
FUZZ_FLAGS="$SANITIZER_FLAGS -fsanitize=fuzzer-no-link -O1 $DEBUG_FLAGS -fno-sanitize=bool,enum"
compile_all "$OUT/obj-fuzz" "$FUZZ_FLAGS" "$FUZZ_FLAGS"
rm -f "$OUT/libcore-fuzz.a"
ar rcs "$OUT/libcore-fuzz.a" "$OUT"/obj-fuzz/*.o

# 2) Harnesses, linked twice: libFuzzer engine + standalone run-once reproducer. LSan is switched off
#    at build time through lsan_off.cc.
LIBS=(-lminizip -lz -ldl)
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -c "$SRC/mayhem/lsan_off.cc" -o "$OUT/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
for h in "${HARNESSES[@]}"; do
  $CXX -std=c++20 $FUZZ_FLAGS "${CORE_DEFS[@]}" "${CORE_INC[@]}" -I"$SRC/mayhem/harnesses" \
      -c "$SRC/mayhem/harnesses/$h.cpp" -o "$OUT/$h.o"
  $CXX $FUZZ_FLAGS $LIB_FUZZING_ENGINE "$OUT/$h.o" "$OUT/lsan_off.o" "$OUT/libcore-fuzz.a" "${LIBS[@]}" -o "$OUT/$h"
  $CXX $FUZZ_FLAGS "$OUT/$h.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" "$OUT/libcore-fuzz.a" "${LIBS[@]}" -o "$OUT/$h-standalone"
done

# 3) The functional self-test, with the project's NORMAL flags (no sanitizers, independent objects).
PLAIN_FLAGS="-O2 $COVERAGE_FLAGS"
LZMA_RELAX=""
compile_all "$OUT/obj-plain" "$PLAIN_FLAGS" "$PLAIN_FLAGS"
rm -f "$OUT/libcore-plain.a"
ar rcs "$OUT/libcore-plain.a" "$OUT"/obj-plain/*.o
$CXX -std=c++20 $PLAIN_FLAGS "${CORE_DEFS[@]}" "${CORE_INC[@]}" "$SRC/mayhem/tests/selftest.cpp" \
    "$OUT/libcore-plain.a" "${LIBS[@]}" -o "$OUT/selftest"
