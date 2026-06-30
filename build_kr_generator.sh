#!/usr/bin/env bash
# build_kr_generator.sh
# Compiles GeneratorKrDecay.cxx into a shared library.
# Run from within the O2 alienv environment (alienv enter O2/latest).
#
# Library names from Generators/CMakeLists.txt PUBLIC_LINK_LIBRARIES:
#   FairRoot::Base -> libBase
#   FairRoot::Gen  -> libGen
#   O2::SimulationDataFormat -> libO2SimulationDataFormat
#   O2::Generators -> libO2Generators

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="${SCRIPT_DIR}/GeneratorKrDecay.cxx"
HDR="${SCRIPT_DIR}/GeneratorKrDecay.h"

if [[ ! -f "$SRC" ]]; then echo "ERROR: GeneratorKrDecay.cxx not found"; exit 1; fi
if [[ ! -f "$HDR" ]]; then echo "ERROR: GeneratorKrDecay.h not found"; exit 1; fi

# Derive paths from environment (alienv exports these directly) with a
# grep-based fallback for non-standard build layouts.
extract_from_ldpath() {
  echo "$LD_LIBRARY_PATH" | tr ':' '\n' | grep "$1" | head -1 | sed 's|/lib/*$||'
}

: "${O2_ROOT:=$(extract_from_ldpath "/O2/")}"
: "${FAIRROOT_ROOT:=$(extract_from_ldpath "/FairRoot/")}"
: "${ROOTSYS:=$(root-config --prefix 2>/dev/null)}"
# FairLogger may be a separate package or bundled inside FairRoot
: "${FAIRLOGGER_ROOT:=$(extract_from_ldpath "/FairLogger/")}"

[[ -z "$O2_ROOT" ]]       && echo "ERROR: O2_ROOT not set and not found in LD_LIBRARY_PATH"       && exit 1
[[ -z "$FAIRROOT_ROOT" ]] && echo "ERROR: FAIRROOT_ROOT not set and not found in LD_LIBRARY_PATH" && exit 1
[[ -z "$ROOTSYS" ]]       && echo "ERROR: ROOTSYS not set and root-config --prefix failed"        && exit 1

echo "[INFO] O2_ROOT       = ${O2_ROOT}"
echo "[INFO] FAIRROOT_ROOT = ${FAIRROOT_ROOT}"
echo "[INFO] ROOTSYS       = ${ROOTSYS}"
[[ -n "$FAIRLOGGER_ROOT" ]] && echo "[INFO] FAIRLOGGER_ROOT = ${FAIRLOGGER_ROOT}"

# FairLogger include: separate package if FAIRLOGGER_ROOT is set, otherwise
# it ships inside FairRoot (common in recent alienv builds).
if [[ -n "$FAIRLOGGER_ROOT" ]]; then
  FAIRLOGGER_INC="-I${FAIRLOGGER_ROOT}/include"
elif [[ -d "${FAIRROOT_ROOT}/include/fairlogger" ]]; then
  FAIRLOGGER_INC="-I${FAIRROOT_ROOT}/include"
else
  FAIRLOGGER_INC=""
  echo "[WARN] FairLogger headers not found separately; relying on -I FairRoot/include"
fi

# Platform
OS=$(uname -s)
if [[ "$OS" == "Darwin" ]]; then
  LIB="/tmp/libGeneratorKrDecay.dylib"
  SHARED_FLAG="-dynamiclib -undefined dynamic_lookup"
else
  LIB="/tmp/libGeneratorKrDecay.so"
  SHARED_FLAG="-shared -fPIC"
fi
RPATH_FLAG="-Wl,-rpath,${O2_ROOT}/lib -Wl,-rpath,${FAIRROOT_ROOT}/lib -Wl,-rpath,${ROOTSYS}/lib"

# Compiler flags
ROOT_CFLAGS=$(root-config --cflags 2>/dev/null || echo "-I${ROOTSYS}/include")
ROOT_LIBS=$(root-config --libs 2>/dev/null || echo "-lCore -lRIO -lTree -lPhysics")

# Copy sources to /tmp to avoid spaces in path
TMP_SRC="/tmp/GeneratorKrDecay_build.cxx"
TMP_HDR="/tmp/GeneratorKrDecay.h"
cp "${SRC}" "${TMP_SRC}"
cp "${HDR}" "${TMP_HDR}"

echo "[INFO] Compiling ${LIB}..."

set -x
c++ -std=c++17 -O2 \
  ${SHARED_FLAG} \
  ${ROOT_CFLAGS} \
  -I/tmp \
  -I"${O2_ROOT}/include" \
  -I"${FAIRROOT_ROOT}/include" \
  ${FAIRLOGGER_INC} \
  -I"${ROOTSYS}/include" \
  -L"${O2_ROOT}/lib" \
  -L"${FAIRROOT_ROOT}/lib" \
  -L"${ROOTSYS}/lib" \
  ${RPATH_FLAG} \
  ${ROOT_LIBS} \
  -lBase \
  -lGen \
  -lO2SimulationDataFormat \
  -lO2Generators \
  -o "${LIB}" \
  "${TMP_SRC}"
set +x

rm -f "${TMP_SRC}" "${TMP_HDR}"

echo ""
echo "[INFO] Built: ${LIB}"
echo "Now run: ./run_kr_sim.sh -n 1000"
